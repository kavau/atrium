#include "session_runner.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <systemd/sd-login.h>
#include <unistd.h>

#include "auth.h"
#include "compositor.h"
#include "daemon/core/bus.h"
#include "daemon/policy/config.h"
#include "daemon/core/seat.h"
#include "daemon/core/vt.h"
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

/* SIGTERM handler: kill the current child so it exits cleanly, then let the
runner's normal wait path detect the exit and clean up. */
static void on_sigterm(int sig) {
    (void)sig;
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

/* Parse credentials from greeter IPC message. Wire format:
"username\0password\0[session_id\0]" (session_id is optional for backward
compatibility). On success, *username, *password, *session_id point into buf.
Returns 0 on success or -1 on error. */
static int parse_credentials(char *buf, ssize_t n, const char **username, const char **password,
                             const char **session_id) {
    if (n <= 0)
        return -1;
    *username = buf;
    size_t ulen = strnlen(*username, (size_t)n);
    if (ulen >= (size_t)n)
        return -1;
    *password = buf + ulen + 1;
    size_t remaining = (size_t)n - ulen - 1;
    size_t plen = strnlen(*password, remaining);
    if (plen >= remaining)
        return -1;
    remaining -= plen + 1;
    if (remaining > 0) { /* session_id is optional */
        *session_id = buf + ulen + 1 + plen + 1;
        if (strnlen(*session_id, remaining) >= remaining)
            return -1;
    } else {
        *session_id = "";
    }
    return 0;
}

/* Wait for a child process to exit and log the result. Retries on EINTR. Logs
an error if waitpid fails. */
static void wait_child(pid_t pid, const char *desc, const char *seat_name) {
    int   wstatus = 0;
    pid_t r;
    do {
        r = waitpid(pid, &wstatus, 0);
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

_Noreturn void session_runner(const char *pam_conf_path, const seat *s) {
    assert(pam_conf_path);
    assert(s);

    /* Ignore SIGPIPE to prevent a broken IPC pipe from killing this process. */
    signal(SIGPIPE, SIG_IGN);

    /* Install handlers for SIGTERM (so runner_stop() can cleanly shut us down)
    and SIGUSR1 (shut down unless a user session is running). */
    struct sigaction sa = {.sa_handler = on_sigterm, .sa_flags = 0};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = on_sigusr1;
    sigaction(SIGUSR1, &sa, NULL);

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

    /* Build PAM environment once; reused across credential attempts.
    XDG_SEAT [XDG_VTNR] XDG_SESSION_TYPE XDG_SESSION_CLASS */
    int    n_env = 4 + (s->vtnr > 0 ? 1 : 0);
    char **pam_env = calloc(n_env, sizeof(*pam_env));
    if (!pam_env) {
        log_syserr("session_runner: calloc");
        kill_and_wait(greeter_pid, "greeter", s->name);
        _exit(EXIT_FAILURE);
    }
    int i = 0;
    if (asprintf(&pam_env[i++], "XDG_SEAT=%s", s->name) < 0) {
        log_error("session_runner: out of memory");
        kill_and_wait(greeter_pid, "greeter", s->name);
        _exit(EXIT_FAILURE);
    }
    if (s->vtnr > 0 && asprintf(&pam_env[i++], "XDG_VTNR=%d", s->vtnr) < 0) {
        log_error("session_runner: out of memory");
        kill_and_wait(greeter_pid, "greeter", s->name);
        _exit(EXIT_FAILURE);
    }
    pam_env[i++] = "XDG_SESSION_TYPE=wayland";
    pam_env[i++] = "XDG_SESSION_CLASS=user";
    pam_env[i++] = NULL;
    assert(i == n_env);

    char        cred_buf[MAX_LEN_IPC_MSG]; /* must remain valid outside the loop */
    auth_result pam_result;
    const char *username = NULL;
    const char *chosen_session = NULL; /* points into cred_buf; valid after loop */
    while (1) {
        ssize_t n = ipc_recv(parent_end, cred_buf, sizeof(cred_buf) - 1);
        if (n <= 0) {
            if (g_reload_requested)
                log_info("session_runner: terminating due to reload request on seat '%s'", s->name);
            else
                log_error("session_runner: greeter disconnected before auth on seat '%s'", s->name);
            kill_and_wait(greeter_pid, "greeter", s->name);
            _exit(g_reload_requested ? EXIT_SUCCESS : EXIT_FAILURE);
        }

        const char *password;
        if (parse_credentials(cred_buf, n, &username, &password, &chosen_session) < 0) {
            log_warn("session_runner: invalid credentials from greeter on seat '%s'", s->name);
            ipc_send_str(parent_end, "fail:invalid credentials\n");
            continue;
        }

        if (!is_valid_username(username)) {
            log_warn("session_runner: invalid username from greeter on seat '%s'", s->name);
            ipc_send_str(parent_end, "fail:invalid username\n");
            continue;
        }

        /* Phase 1: verify user credentials. */
        int auth_r = auth_authenticate(username, password, (const char **)pam_env, pam_conf_path,
                                       "atrium", &pam_result);
        if (auth_r != PAM_SUCCESS) {
            log_warn("session_runner: auth failed for '%s' on seat '%s'", username, s->name);
            ipc_send_str(parent_end, "fail:authentication failed\n");
            continue;
        }

        /* Duplicate login check: between authenticate and open_session so the
        password is verified before we reveal the duplicate status. */
        if (!config_allow_duplicate_login()) {
            struct passwd *pw = getpwnam(username);
            if (!pw) {
                log_syserr("session_runner: getpwnam(%s)", username);
                ipc_send_str(parent_end, "fail:system error\n");
                auth_cancel(&pam_result);
                continue;
            }

            login_lock_status lock_status = acquire_login_lock(pw->pw_uid);
            if (lock_status == LOGIN_LOCK_DUPLICATE) {
                log_info("session_runner: user '%s' already logged in on another seat", username);
                ipc_send_str(parent_end, "fail:User already logged in on another seat\n");
                auth_cancel(&pam_result);
                continue;
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
            ipc_send_str(parent_end, "fail:session error\n");
            release_login_lock();
            user_session_active = 0;
            continue;
        }

        log_info("session_runner: auth ok for '%s' on seat '%s'", username, s->name);
        if (chosen_session && chosen_session[0] != '\0')
            sessions_save_seat(s->name, chosen_session);
        ipc_send_str(parent_end, "ok\n");
        break;
    }

    free(pam_env[0]); /* XDG_SEAT */
    if (s->vtnr > 0)
        free(pam_env[1]); /* XDG_VTNR */
    free(pam_env);

    /* Greeter exits after reading "ok\n". Wait for it to exit cleanly; send
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

    /* Activate VT for seat0; blocks until active. */
    if (s->vtnr > 0 && vt_activate(s->vtnr) < 0) {
        log_error("session_runner: failed to activate VT%d", s->vtnr);
        auth_close_session(&pam_result);
        _exit(EXIT_FAILURE);
    }

    /* Fork the compositor child. */
    pid_t comp_pid = fork();
    if (comp_pid < 0) {
        log_syserr("session_runner: fork (compositor)");
        auth_close_session(&pam_result);
        _exit(EXIT_FAILURE);
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

    wait_child(comp_pid, "compositor", s->name);
    g_child_pid = 0;
    user_session_active = 0;

    /* Re-suppress VT keyboard (compositor may have re-enabled it on exit) */
    if (s->vtnr > 0)
        vt_suppress_keyboard(s->vtnr, NULL);

    auth_close_session(&pam_result);
    release_login_lock();
    log_debug("session lifecycle complete on seat '%s'", s->name);
    _exit(EXIT_SUCCESS);
}
