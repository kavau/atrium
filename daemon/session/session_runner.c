#include "session_runner.h"

#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "auth.h"
#include "bus.h"
#include "daemon/core/seat.h"
#include "daemon/core/vt.h"
#include "lib/defs.h"
#include "lib/ipc.h"
#include "lib/log.h"
#include "session_compositor.h"
#include "session_greeter.h"

/* Parse credentials from greeter IPC message (format: "username\0password\0").
On success, *username and *password point into buf. Returns 0 or -1. */
static int parse_credentials(char *buf, ssize_t n, const char **username, const char **password) {
    if (n <= 0)
        return -1;
    *username = buf;
    size_t ulen = strnlen(buf, (size_t)n);
    if (ulen >= (size_t)n)
        return -1;
    *password = buf + ulen + 1;
    size_t remaining = (size_t)n - ulen - 1;
    if (strnlen(*password, remaining) >= remaining)
        return -1;
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

_Noreturn void session_runner(const char *pam_conf_path, const seat *s) {
    assert(pam_conf_path);
    assert(s);

    /* ---- GREETER PHASE ---- */

    /* Create IPC channel for the greeter to communicate credentials. */
    ipc_channel *parent_end, *child_end;
    if (ipc_create(&parent_end, &child_end) < 0) {
        log_syserr("session_runner: ipc_create");
        _exit(EXIT_FAILURE);
    }

    struct passwd *pw = getpwnam(GREETER_USERNAME);
    if (!pw) {
        log_error("session_runner: getpwnam failed for '%s'", GREETER_USERNAME);
        ipc_close(parent_end);
        ipc_close(child_end);
        _exit(EXIT_FAILURE);
    }

    pid_t greeter_pid = fork();
    if (greeter_pid < 0) {
        log_syserr("session_runner: fork (greeter)");
        ipc_close(parent_end);
        ipc_close(child_end);
        _exit(EXIT_FAILURE);
    }

    if (greeter_pid == 0) {
        /* Child process: execute greeter. */
        ipc_close(parent_end);
        child_exec_greeter(GREETER_USERNAME, s, child_end);
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
        ipc_close(parent_end);
        waitpid(greeter_pid, NULL, 0);
        _exit(EXIT_FAILURE);
    }

    if (bus_create_session(s->name, (uint32_t)s->vtnr, pw->pw_uid, greeter_pid, "", "greeter",
                           session_id, sizeof(session_id), session_obj, sizeof(session_obj),
                           runtime_path, sizeof(runtime_path), &fifo_fd) < 0) {
        ipc_close(parent_end);
        waitpid(greeter_pid, NULL, 0);
        bus_close();
        _exit(EXIT_FAILURE);
    }

    log_info("session_runner: greeter session %s started (PID %d) on seat '%s'", session_id,
             (int)greeter_pid, s->name);

    if (s->vtnr > 0 && bus_activate_session(session_obj) < 0) {
        ipc_close(parent_end);
        waitpid(greeter_pid, NULL, 0);
        close(fifo_fd);
        bus_close();
        _exit(EXIT_FAILURE);
    }

    /* SHORTCUT: wait to make sure session is active. */
    sleep(1);

    /* Signal greeter to proceed by sending the session_id. */
    if (ipc_send(parent_end, session_id, strlen(session_id)) < 0)
        log_syserr("session_runner: ipc_send session_id");

    wait_child(greeter_pid, "greeter", s->name);

    /* Close fifo_fd to signal logind that the greeter session has ended. */
    close(fifo_fd);
    bus_close();

    /* ---- READ CREDENTIALS ---- */

    char cred_buf[256];
    ssize_t n = ipc_recv(parent_end, cred_buf, sizeof(cred_buf) - 1);
    ipc_close(parent_end);

    const char *username, *password;
    if (n <= 0 || parse_credentials(cred_buf, n, &username, &password) < 0) {
        log_error("session_runner: failed to read credentials from greeter on seat '%s'", s->name);
        _exit(EXIT_FAILURE);
    }

    /* ---- USER SESSION PHASE ---- */

    /* Build PAM environment. */
    int n_env = 4 + (s->vtnr > 0 ? 1 : 0);
    char **env = calloc(n_env, sizeof(*env));
    if (!env) {
        log_syserr("session_runner: calloc");
        _exit(EXIT_FAILURE);
    }
    int i = 0;
    if (asprintf(&env[i++], "XDG_SEAT=%s", s->name) < 0) {
        log_error("session_runner: out of memory");
        _exit(EXIT_FAILURE);
    }
    if (s->vtnr > 0 && asprintf(&env[i++], "XDG_VTNR=%d", s->vtnr) < 0) {
        log_error("session_runner: out of memory");
        _exit(EXIT_FAILURE);
    }
    env[i++] = "XDG_SESSION_TYPE=wayland";
    env[i++] = "XDG_SESSION_CLASS=user";
    env[i++] = NULL;
    assert(i == n_env);

    auth_result pam_result;
    int auth_r = auth_open_session(username, password, (const char **)env, pam_conf_path,
                                   "atrium-dev", &pam_result);

    free(env[0]); /* XDG_SEAT */
    if (s->vtnr > 0)
        free(env[1]); /* XDG_VTNR */
    free(env);

    if (auth_r != PAM_SUCCESS) {
        log_error("session_runner: PAM auth failed on seat '%s': %d", s->name, auth_r);
        _exit(EXIT_FAILURE);
    }

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
        child_exec_compositor(username, &pam_result);
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
