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
#include <time.h>
#include <unistd.h>

#include "auth.h"
#include "daemon/core/bus.h"
#include "daemon/core/seat.h"
#include "daemon/core/vt.h"
#include "lib/defs.h"
#include "lib/ipc.h"
#include "lib/log.h"
#include "lib/proc.h"
#include "compositor.h"
#include "greeter.h"
#include "sessions.h"

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
static int parse_credentials(char *buf, ssize_t n, const char **username,
                              const char **password, const char **session_id) {
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
    int wstatus = 0;
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
        long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
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

    /* ---- GREETER PHASE ---- */

    /* Scan available Wayland sessions and serialize for the greeter. */
    char session_list[4096] = "";
    if (sessions_scan() > 0) {
        size_t pos = 0;
        for (const session_entry *e = sessions_first(); e; e = sessions_next(e)) {
            int w = snprintf(session_list + pos, sizeof(session_list) - pos,
                             "%s\x1f%s\x1e", e->id, e->name);
            if (w < 0 || (size_t)w >= sizeof(session_list) - pos) {
                log_warn("session_runner: session list truncated");
                break;
            }
            pos += (size_t)w;
        }
    }

    /* Create IPC channel for the greeter to communicate credentials. */
    ipc_channel *parent_end, *child_end;
    if (ipc_create(&parent_end, &child_end) < 0) {
        log_syserr("session_runner: ipc_create");
        _exit(EXIT_FAILURE);
    }

    /* Do as much as possible before the fork so error cleanup is easier. */
    struct passwd *pw = getpwnam(GREETER_USERNAME);
    if (!pw) {
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
        child_exec_greeter(GREETER_USERNAME, s, child_end, session_list);
        /* unreachable */
    }

    /* Parent process: greeter owns child_end. */
    ipc_close(child_end);

    /* CreateSession for greeter, using greeter_pid as the session leader. */
    char session_id[32] = {0};
    char session_obj[256] = {0};
    char runtime_path[64] = {0};
    int fifo_fd = -1;

    if (bus_open() < 0) {
        kill_and_wait(greeter_pid, "greeter", s->name);
        _exit(EXIT_FAILURE);
    }

    if (bus_create_session(s->name, (uint32_t)s->vtnr, pw->pw_uid, greeter_pid, "", "greeter",
                           session_id, sizeof(session_id), session_obj, sizeof(session_obj),
                           runtime_path, sizeof(runtime_path), &fifo_fd) < 0) {
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
    int n_env = 4 + (s->vtnr > 0 ? 1 : 0);
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

    char cred_buf[256]; /* must remain valid outside the loop */
    auth_result pam_result;
    const char *username       = NULL;
    const char *chosen_session = NULL; /* points into cred_buf; valid after loop */
    while (1) {
        ssize_t n = ipc_recv(parent_end, cred_buf, sizeof(cred_buf) - 1);
        if (n <= 0) {
            log_error("session_runner: greeter disconnected before auth on seat '%s'", s->name);
            kill_and_wait(greeter_pid, "greeter", s->name);
            _exit(EXIT_FAILURE);
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

        int auth_r = auth_open_session(username, password, (const char **)pam_env, pam_conf_path,
                                       "atrium", &pam_result);

        if (auth_r != PAM_SUCCESS) {
            /* auth_open_session already logged the PAM error. */
            log_warn("session_runner: auth failed for '%s' on seat '%s'", username, s->name);
            ipc_send_str(parent_end, "fail:authentication failed\n");
            continue;
        }

        log_info("session_runner: auth ok for '%s' on seat '%s'", username, s->name);
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

    /* Parent process - wait for compositor exit */
    log_info("started user session for '%s' (PID %d) on seat '%s'", username, (int)comp_pid,
             s->name);

    wait_child(comp_pid, "compositor", s->name);

    auth_close_session(&pam_result);
    log_debug("session lifecycle complete on seat '%s'", s->name);
    _exit(EXIT_SUCCESS);
}
