#include "session_runner.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <systemd/sd-login.h>
#include <unistd.h>

#include "auth.h"
#include "compositor.h"
#include "daemon/core/bus.h"
#include "daemon/core/seat.h"
#include "daemon/core/vt.h"
#include "daemon/policy/config.h"
#include "greeter.h"
#include "lib/defs.h"
#include "lib/ipc.h"
#include "lib/log.h"
#include "lib/proc.h"
#include "lib/time_util.h"
#include "lock.h"
#include "sessions.h"

/* PID of the runner's current child (greeter or compositor). Cleared after the
child exits. 0 means no child is currently active. */
static volatile sig_atomic_t g_child_pid = 0;

static volatile sig_atomic_t user_session_active = 0; /* Set to 1 while user session is active */
static volatile sig_atomic_t g_reload_requested = 0;  /* Set to 1 if SIGUSR1 is received */
static volatile sig_atomic_t g_terminate = 0;         /* Set to 1 if SIGTERM is received. */

/* SIGTERM handler: record the request and kill the current child so it exits
cleanly, then let the runner's normal wait path detect the exit and clean up. */
static void on_sigterm(int sig) {
    (void)sig;
    g_terminate = 1;
    if (g_child_pid > 0)
        kill((pid_t)g_child_pid, SIGTERM);
}

/* SIGUSR1 handler: quit unless a user session is active. */
static void on_sigusr1(int sig) {
    (void)sig;
    if (!user_session_active && g_child_pid > 0) {
        g_reload_requested = 1;
        kill((pid_t)g_child_pid, SIGTERM);
    }
}

/* Validate username: non-empty, within LOGIN_NAME_MAX, portable filename chars
only ([A-Za-z0-9._-]) and optional trailing '$'. Rejects ANSI escape sequences and other
injection. */
static int is_valid_username(const char *username) {
    if (!username || username[0] == '\0')
        return 0;
    size_t len = strlen(username);
    if (len >= LOGIN_NAME_MAX)
        return 0;
    for (size_t i = 0; i < len; i++) {
        char c = username[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.')
            continue;
        if (c == '$' && i == len - 1)
            continue;
        return 0;
    }
    return 1;
}

/* Wait for a child process to exit and log the result. Returns 1 once the child
has been reaped, or 0 if the wait was cut short by a shutdown request. Other
interruptions are retried. Logs an error if waitpid fails. */
static int wait_child(pid_t pid, const char *desc, const char *seat_name) {
    int   wstatus = 0;
    pid_t r;
    do {
        r = waitpid(pid, &wstatus, 0);
        if (r < 0 && errno == EINTR && g_terminate)
            return 0;
    } while (r < 0 && errno == EINTR);

    if (r < 0)
        log_syserr("wait_child: waitpid (%s)", desc);
    else if (WIFEXITED(wstatus))
        log_info("%s exited with status %d on seat '%s'", desc, WEXITSTATUS(wstatus), seat_name);
    else if (WIFSIGNALED(wstatus))
        log_info("%s terminated by signal %d (%s) on seat '%s'", desc, WTERMSIG(wstatus),
                 strsignal(WTERMSIG(wstatus)), seat_name);
    else
        log_warn("%s exited with unexpected status %d on seat '%s'", desc, wstatus, seat_name);
    return 1;
}

/* Release everything the user session holds, then exit. Use on every path after
successful authentication. */
static _Noreturn void close_session_and_exit(auth_result *pam_result, int status) {
    user_session_active = 0;
    auth_close_session(pam_result);
    release_login_lock();
    _exit(status);
}

/* Exit cleanly if SIGTERM has been recorded. */
static void exit_if_terminating(auth_result *pam_result, const char *seat_name) {
    if (!g_terminate)
        return;
    log_info("session_runner: shutdown requested on seat '%s'", seat_name);
    close_session_and_exit(pam_result, EXIT_SUCCESS);
}

/* Wait until logind has activated the session by polling sd_session_is_active().
Returns 0 when active, -1 on timeout or error. */
static int wait_session_active(const char *session_id) {
    const int MAX_POLLS = 100; /* 100 x 20 ms = 2 s ceiling */
    const int POLL_US = 20000; /* 20 ms */
    for (int i = 0; i < MAX_POLLS; i++) {
        int r = sd_session_is_active(session_id);
        if (r > 0) {
            log_info("session %s active (waited %d ms)", session_id, i * 20);
            return 0;
        }
        if (r < 0) {
            log_error("sd_session_is_active(%s): %s", session_id, strerror(-r));
            return -1;
        }
        usleep((useconds_t)POLL_US);
    }
    log_error("session %s: timed out waiting for active state (2 s)", session_id);
    return -1;
}

/* Wait for udevadm settle to complete, to make sure device ACLs have been
applied. TODO: consider using libudev udev_queue_get_queue_is_empty() and
udev_queue_get_fd() instead of forking udevadm. */
static void wait_udev_settle(const char *seat_name) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pid_t settle_pid = fork();
    if (settle_pid == 0) {
        /* child */
        execl("/usr/bin/udevadm", "udevadm", "settle", "--timeout=5", (char *)NULL);
        log_syserr("wait_udev_settle: execl udevadm");
        _exit(127);
    } else if (settle_pid > 0) {
        /* parent */
        int wstatus = 0;
        waitpid(settle_pid, &wstatus, 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long ms = timediff_ms(t0, t1);
        if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
            log_warn("%s: udevadm settle failed with status %d after %ld ms", seat_name,
                     WEXITSTATUS(wstatus), ms);
        else
            log_info("%s: udevadm settle completed in %ld ms", seat_name, ms);
    } else {
        log_syserr("wait_udev_settle: fork");
    }
}

/* Handle a power action message from the greeter. */
static void handle_power_request(const char *msg, ssize_t len, ipc_channel *ch,
                                 const char *seat_name) {
    (void)ch;

    const char *action;
    if (ipc_msg_parse1(msg, len, IPC_TYPE_POWER, &action) < 0) {
        log_warn("session_runner: invalid power request from seat '%s'", seat_name);
        /* TODO(GH#135): the greeter currently cannot process the response. */
        /* ipc_send_str(ch, "fail:invalid power request"); */
        return;
    }
    log_info("session_runner: power request '%s' received on seat '%s'", action, seat_name);
    if (!config_power_actions()) {
        log_warn("session_runner: power actions disabled in config, ignoring request");
        /* TODO(GH#135): the greeter currently cannot process the response. */
        /* ipc_send_str(ch, "fail:not permitted"); */
        return;
    }

    int r = 0;
    if (strcmp(action, "shutdown") == 0) {
        log_info("session_runner: shutting down system on seat '%s'", seat_name);
        r = bus_power_off();
    } else if (strcmp(action, "reboot") == 0) {
        log_info("session_runner: rebooting system on seat '%s'", seat_name);
        r = bus_reboot();
    } else {
        log_warn("session_runner: unknown power action '%s' on seat '%s'", action, seat_name);
        /* TODO(GH#135): the greeter currently cannot process the response. */
        /* ipc_send_str(ch, "fail:unknown power action"); */
    }

    if (r < 0) {
        /* TODO(GH#135): the greeter currently cannot process the response. */
        /* ipc_send_str(ch, "fail:request refused"); */
    }
}

/* Append "key=val" to `env' at index *i, advancing *i on success. */
static int env_add(char **env, int *i, const char *key, const char *val) {
    if (asprintf(&env[*i], "%s=%s", key, val) < 0) {
        env[*i] = NULL; /* keep the array NULL-terminated on failure */
        return -1;
    }
    (*i)++;
    return 0;
}

static void free_pam_env(char **env) {
    if (!env)
        return;
    for (char **p = env; *p; p++)
        free(*p);
    free(env);
}

/* Build the PAM environment for the user session. Every entry is
heap-allocated, so free_pam_env() can just walk the array. */
static char **build_pam_env(const seat *s, const char *desktop) {
    assert(desktop);

    int    n_env = 4 + (s->vtnr > 0 ? 1 : 0) + (*desktop ? 1 : 0);
    char **env = calloc(n_env, sizeof(*env)); /* zeroed: the array is NULL-terminated */
    if (!env)
        return NULL;

    int i = 0;
    if (env_add(env, &i, "XDG_SEAT", s->name) < 0)
        goto oom;
    if (s->vtnr > 0) {
        char vtnr[16];
        snprintf(vtnr, sizeof(vtnr), "%d", s->vtnr);
        if (env_add(env, &i, "XDG_VTNR", vtnr) < 0)
            goto oom;
    }
    if (env_add(env, &i, "XDG_SESSION_TYPE", "wayland") < 0)
        goto oom;
    if (env_add(env, &i, "XDG_SESSION_CLASS", "user") < 0)
        goto oom;
    if (*desktop && env_add(env, &i, "XDG_SESSION_DESKTOP", desktop) < 0)
        goto oom;

    if (*desktop)
        log_debug("build_pam_env: XDG_SESSION_DESKTOP=%s", desktop);
    else
        log_debug("build_pam_env: no desktop name known, XDG_SESSION_DESKTOP omitted");

    assert(i == n_env - 1); /* last slot stays NULL */
    return env;

oom:
    free_pam_env(env);
    return NULL;
}

_Noreturn void session_runner(const char *pam_conf_path, const seat *s) {
    assert(pam_conf_path);
    assert(s);

    /* Suppress core dumps as a precaution (pam_kwallet5 keeps a plaintext
    password in memory until pam_end()) */
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) < 0)
        log_syserr("session_runner: prctl(PR_SET_DUMPABLE)");

    /* Ignore SIGPIPE to prevent a broken IPC pipe from killing this process. */
    signal(SIGPIPE, SIG_IGN);

    /* Install handlers for SIGTERM (so runner_stop() can cleanly shut us down)
    and SIGUSR1 (shut down unless a user session is running). */
    struct sigaction sa = {.sa_handler = on_sigterm, .sa_flags = 0};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = on_sigusr1;
    sigaction(SIGUSR1, &sa, NULL);

    /* Apply this seat's config overrides. We are past the fork, so the daemon's
    copy remains untouched. */
    config_apply_seat_overrides(s->name);

    /* ---- GREETER PHASE ---- */

    /* Scan available Wayland sessions and serialize for greeter (skip if a
    compositor override is configured). */
    char        session_list[4096] = "";
    char        preselect[64] = "";
    const char *compositor_cmd = config_compositor();
    log_debug("session_runner: config compositor '%s'", compositor_cmd);
    if ((!compositor_cmd || !*compositor_cmd) && sessions_scan() > 0) {
        log_debug("session_runner: no compositor override, building session list for greeter");
        size_t pos = 0;
        for (const session_entry *e = sessions_first(); e; e = sessions_next(e)) {
            int w = snprintf(session_list + pos, sizeof(session_list) - pos, "%s\x1f%s\x1e", e->id,
                             e->name);
            if (w < 0 || (size_t)w >= sizeof(session_list) - pos) {
                log_warn("session_runner: session list truncated");
                break;
            }
            pos += (size_t)w;
        }
        sessions_load_seat(s->name, preselect, sizeof(preselect));
    }

    /* Create IPC channel for the greeter to communicate credentials. */
    ipc_channel *parent_end, *child_end;
    if (ipc_create(&parent_end, &child_end) < 0) {
        log_syserr("session_runner: ipc_create");
        _exit(EXIT_FAILURE);
    }

    /* Do as much as possible before the fork so error cleanup is easier. */
    struct passwd *greeter_pw = getpwnam(GREETER_USERNAME);
    if (!greeter_pw) {
        log_error("session_runner: getpwnam failed for '%s'", GREETER_USERNAME);
        _exit(EXIT_FAILURE);
    }

    pid_t greeter_pid = fork();
    if (greeter_pid < 0) {
        log_syserr("session_runner: fork (greeter)");
        _exit(EXIT_FAILURE);
    }
    if (greeter_pid == 0) {
        /* Child process: execute greeter. */
        ipc_close(parent_end);
        child_exec_greeter(GREETER_USERNAME, s, child_end, session_list, preselect);
        /* unreachable */
    }
    g_child_pid = (sig_atomic_t)greeter_pid;

    /* Parent process: greeter owns child_end. */
    ipc_close(child_end);

    /* CreateSession for greeter, using greeter_pid as the session leader. */
    char session_id[32] = {0};
    char session_obj[256] = {0};
    char runtime_path[64] = {0};
    int  fifo_fd = -1;

    if (bus_open() < 0) {
        kill_and_wait(greeter_pid, "greeter", s->name);
        _exit(EXIT_FAILURE);
    }

    if (bus_create_session(s->name, (uint32_t)s->vtnr, greeter_pw->pw_uid, greeter_pid, "",
                           "greeter", session_id, sizeof(session_id), session_obj,
                           sizeof(session_obj), runtime_path, sizeof(runtime_path), &fifo_fd) < 0) {
        kill_and_wait(greeter_pid, "greeter", s->name);
        _exit(EXIT_FAILURE);
    }

    log_info("session_runner: greeter session %s started (PID %d) on seat '%s'", session_id,
             (int)greeter_pid, s->name);

    if (s->vtnr > 0 && bus_activate_session(session_obj) < 0) {
        kill_and_wait(greeter_pid, "greeter", s->name);
        _exit(EXIT_FAILURE);
    }

    if (wait_session_active(session_id) < 0) {
        log_error("session_runner: session %s never became active; aborting on seat '%s'",
                  session_id, s->name);
        kill_and_wait(greeter_pid, "greeter", s->name);
        _exit(EXIT_FAILURE);
    }
    wait_udev_settle(s->name);

    /* Signal greeter to proceed by sending the session_id. */
    if (ipc_send(parent_end, session_id, strlen(session_id)) < 0)
        log_syserr("session_runner: ipc_send session_id");

    /* ---- CREDENTIAL / AUTH LOOP ---- */

    /* +1 for the NUL ipc_recv_str() adds. cred_buf must remain valid outside
    the loop since username and chosen_session point into it. */
    char        cred_buf[MAX_LEN_IPC_MSG + 1];
    auth_result pam_result;
    const char *username = NULL;
    const char *chosen_session = NULL;
    while (1) {
        ssize_t n = ipc_recv_str(parent_end, cred_buf, sizeof(cred_buf));
        if (n <= 0) {
            if (g_reload_requested)
                log_info("session_runner: terminating due to reload request on seat '%s'", s->name);
            else if (g_terminate)
                log_info("session_runner: terminating due to shutdown on seat '%s'", s->name);
            else
                log_error("session_runner: greeter disconnected before auth on seat '%s'", s->name);
            kill_and_wait(greeter_pid, "greeter", s->name);
            _exit(g_reload_requested || g_terminate ? EXIT_SUCCESS : EXIT_FAILURE);
        }

        /* Check message type. We accept credentials and power action messages. */
        const char *tag = ipc_msg_type(cred_buf, n);
        if (tag && strcmp(tag, IPC_TYPE_POWER) == 0) {
            handle_power_request(cred_buf, n, parent_end, s->name);
            goto retry;
        }

        if (!tag || strcmp(tag, IPC_TYPE_CRED) != 0) {
            log_warn("session_runner: unknown message type '%s' from greeter on seat '%s'",
                     tag ? tag : "(untagged)", s->name);
            ipc_send_str(parent_end, "fail:invalid message");
            goto retry;
        }

        const char *password;
        if (ipc_msg_parse3(cred_buf, n, IPC_TYPE_CRED, &username, &password, &chosen_session) < 0) {
            log_warn("session_runner: invalid credentials from greeter on seat '%s'", s->name);
            ipc_send_str(parent_end, "fail:invalid credentials");
            goto retry;
        }

        if (!is_valid_username(username)) {
            log_warn("session_runner: invalid username from greeter on seat '%s'", s->name);
            ipc_send_str(parent_end, "fail:invalid username");
            goto retry;
        }

        /* Phase 1: verify user credentials. The PAM environment is rebuilt here
        because the chosen session can change between attempts. */
        char **pam_env = build_pam_env(s, session_resolve(chosen_session).session_desktop);
        if (!pam_env) {
            log_error("session_runner: out of memory building PAM environment");
            kill_and_wait(greeter_pid, "greeter", s->name);
            _exit(EXIT_FAILURE);
        }
        int auth_r = auth_authenticate(username, password, (const char **)pam_env, pam_conf_path,
                                       "atrium", &pam_result);
        free_pam_env(pam_env);
        /* password is not needed beyond this point, wipe it for security.
        username and chosen_session (also in cred_buf) must remain intact. */
        explicit_bzero((char *)password, strlen(password));
        if (auth_r != PAM_SUCCESS) {
            char reply[MAX_LEN_IPC_MSG];
            snprintf(reply, sizeof(reply), "fail:%s", auth_fail_message(auth_r));
            log_warn("session_runner: auth failed for '%s' on seat '%s': %s", username, s->name,
                     pam_strerror(NULL, auth_r));
            ipc_send_str(parent_end, reply);
            goto retry;
        }

        /* Duplicate login check: between authenticate and open_session so the
        password is verified before we reveal the duplicate status. */
        if (!config_allow_duplicate_login()) {
            struct passwd *pw = getpwnam(username);
            if (!pw) {
                log_syserr("session_runner: getpwnam(%s)", username);
                ipc_send_str(parent_end, "fail:system error");
                auth_cancel(&pam_result);
                goto retry;
            }

            login_lock_status lock_status = acquire_login_lock(pw->pw_uid);
            if (lock_status == LOGIN_LOCK_DUPLICATE) {
                log_info("session_runner: user '%s' already logged in on another seat", username);
                ipc_send_str(parent_end, "fail:User already logged in on another seat");
                auth_cancel(&pam_result);
                goto retry;
            }
            if (lock_status == LOGIN_LOCK_ERROR) {
                /* System error acquiring lock; allow login with warning */
                log_warn("session_runner: couldn't acquire login lock for '%s' (allowing anyway)",
                         username);
            }
        }

        user_session_active = 1; /* greeter phase complete, user session becomes active */

        /* Phase 2: open the logind session. */
        if (auth_open_session(&pam_result) != PAM_SUCCESS) {
            log_warn("session_runner: failed to open session for '%s' on seat '%s'", username,
                     s->name);
            ipc_send_str(parent_end, "fail:session error");
            release_login_lock();
            user_session_active = 0;
            goto retry;
        }

        log_info("session_runner: auth ok for '%s' on seat '%s'", username, s->name);
        if (chosen_session && chosen_session[0] != '\0')
            sessions_save_seat(s->name, chosen_session);
        ipc_send_str(parent_end, "ok");
        break;

    retry:
        /* cred_buf contains the plaintext password - explicitly wipe it for security. */
        explicit_bzero(cred_buf, sizeof(cred_buf));
        username = NULL;
        chosen_session = NULL;
    }

    /* Greeter exits after reading "ok". Wait for it to exit cleanly; send
    SIGTERM and SIGKILL only if it does not exit within 5 s. */
    wait_and_kill(greeter_pid, "greeter", s->name);
    g_child_pid = 0;

    /* Re-suppress VT keyboard (cage restores K_UNICODE on exit) */
    if (s->vtnr > 0)
        vt_suppress_keyboard(s->vtnr, NULL);

    ipc_close(parent_end);
    close(fifo_fd); /* signals logind that the session has ended */
    bus_close();

    /* ---- USER SESSION PHASE ---- */

    exit_if_terminating(&pam_result, s->name);

    /* Activate VT for seat0; blocks until active. */
    if (s->vtnr > 0 && vt_activate(s->vtnr) < 0) {
        log_error("session_runner: failed to activate VT%d", s->vtnr);
        close_session_and_exit(&pam_result, EXIT_FAILURE);
    }

    exit_if_terminating(&pam_result, s->name); /* re-check since vt_activate() blocks */

    /* Fork the compositor child. */
    pid_t comp_pid = fork();
    if (comp_pid < 0) {
        log_syserr("session_runner: fork (compositor)");
        close_session_and_exit(&pam_result, EXIT_FAILURE);
    }
    if (comp_pid == 0) {
        /* Child process - execute compositor */
        child_exec_compositor(username, &pam_result, chosen_session ? chosen_session : "");
        /* unreachable */
    }
    g_child_pid = (sig_atomic_t)comp_pid;

    /* Parent process - wait for compositor exit */
    log_info("started user session for '%s' (PID %d) on seat '%s'", username, (int)comp_pid,
             s->name);

    if (g_terminate || !wait_child(comp_pid, "compositor", s->name)) {
        /* We received SIGTERM with the session still running. The handler has already
        sent SIGTERM to the compositor; give it a grace period then SIGKILL it. */
        log_info("session_runner: shutdown requested, stopping user session on seat '%s'", s->name);
        kill_and_wait(comp_pid, "compositor", s->name);
    }
    g_child_pid = 0;

    /* Re-suppress VT keyboard (compositor may have re-enabled it on exit) */
    if (s->vtnr > 0)
        vt_suppress_keyboard(s->vtnr, NULL);

    log_debug("session lifecycle complete on seat '%s'", s->name);
    close_session_and_exit(&pam_result, EXIT_SUCCESS);
}
