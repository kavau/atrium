#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
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
#include "session_runner.h"

/* Parse credentials from greeter IPC message (format: "username\0password\0").
 * On success, *username and *password point into buf. Returns 0 or -1. */
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

/* Configure the environment, drop privileges, and exec the greeter. Blocks on
 * sync_read_fd until the parent writes the session_id after CreateSession
 * completes.
 * Called from the child side of the fork. This function never returns.
 * TODO: replace sync pipe with IPC channel. */
static _Noreturn void child_exec_greeter(const char *username, const seat *s,
                                         const char *runtime_dir, int sync_read_fd,
                                         ipc_channel *ch) {
    /* Block until parent signals us by writing the session_id. EOF means the
     * parent failed (e.g. CreateSession error). */
    char session_id[32] = {0};
    ssize_t n = read(sync_read_fd, session_id, sizeof(session_id) - 1);
    close(sync_read_fd);
    if (n <= 0) {
        log_error("child_exec_greeter: sync pipe: parent failed");
        _exit(EXIT_FAILURE);
    }

    struct passwd *pw = getpwnam(username);
    if (!pw) {
        log_error("child_exec_greeter: getpwnam failed for '%s'", username);
        _exit(EXIT_FAILURE);
    }

    /* Build the session environment:
    USER LOGNAME PATH XDG_SEAT [XDG_VTNR] XDG_SESSION_TYPE XDG_SESSION_CLASS
    XDG_RUNTIME_DIR XDG_SESSION_ID WLR_LIBINPUT_NO_DEVICES NULL */
    int n_env = 10 + (s->vtnr > 0 ? 1 : 0);
    char **env = calloc(n_env, sizeof(*env));
    if (!env) {
        log_syserr("child_exec_greeter: calloc");
        _exit(EXIT_FAILURE);
    }

    int i = 0;
    if (asprintf(&env[i++], "USER=%s", pw->pw_name) < 0)
        goto oom;
    if (asprintf(&env[i++], "LOGNAME=%s", pw->pw_name) < 0)
        goto oom;
    env[i++] = "PATH=/usr/local/bin:/usr/bin:/bin";
    if (asprintf(&env[i++], "XDG_SEAT=%s", s->name) < 0)
        goto oom;
    if (s->vtnr > 0) {
        if (asprintf(&env[i++], "XDG_VTNR=%d", s->vtnr) < 0)
            goto oom;
    }
    env[i++] = "XDG_SESSION_TYPE=wayland";
    env[i++] = "XDG_SESSION_CLASS=greeter";
    if (asprintf(&env[i++], "XDG_RUNTIME_DIR=%s", runtime_dir) < 0)
        goto oom;
    if (asprintf(&env[i++], "XDG_SESSION_ID=%s", session_id) < 0)
        goto oom;
    env[i++] = "WLR_LIBINPUT_NO_DEVICES=1";
    env[i++] = NULL;
    assert(i == n_env);

    /* Privilege drop: supplementary groups, then gid, then uid.
     * setresgid/setresuid set all three ID slots (real, effective, saved)
     * atomically. */
    if (initgroups(pw->pw_name, pw->pw_gid) < 0) {
        log_syserr("child_exec_greeter: initgroups");
        _exit(EXIT_FAILURE);
    }
    if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0) {
        log_syserr("child_exec_greeter: setresgid");
        _exit(EXIT_FAILURE);
    }
    if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
        log_syserr("child_exec_greeter: setresuid");
        _exit(EXIT_FAILURE);
    }

    /* Defence-in-depth: verify we cannot re-escalate to root. */
    if (setresuid(0, 0, 0) == 0) {
        log_error("CRITICAL: re-escalation to root succeeded after privilege drop");
        _exit(EXIT_FAILURE);
    }

    if (chdir(pw->pw_dir) < 0)
        log_syserr("child_exec_greeter: chdir"); /* not fatal */

    /* Pass the IPC channel to the greeter via the command line. */
    char cmd[512];
    if (ch) {
        if (ipc_prepare_for_exec(ch) < 0) {
            log_syserr("child_exec_greeter: ipc_prepare_for_exec");
            _exit(EXIT_FAILURE);
        }
        char fd_args[64];
        ipc_fmt_args(ch, fd_args, sizeof(fd_args));
        snprintf(cmd, sizeof(cmd), "%s %s", GREETER, fd_args);
    } else {
        snprintf(cmd, sizeof(cmd), "%s", GREETER);
    }

    log_debug("child_exec_greeter: exec: %s", cmd);
    /* TODO: running the greeter in a shell is unnecessary overhead. */
    execle("/bin/sh", "sh", "-c", cmd, NULL, env);
    log_syserr("child_exec_greeter: execle");
    _exit(EXIT_FAILURE);

oom:
    log_error("child_exec_greeter: out of memory");
    _exit(EXIT_FAILURE);
}

/* Configure the environment, drop privileges, and exec the user compositor.
 * Called from the child side of the fork. This function never returns. */
static _Noreturn void child_exec(const char *username, const auth_result *pam_result) {
    assert(username);
    assert(pam_result);
    assert(pam_result->pam_handle);

#ifdef ATRIUM_DEBUG
    if (pam_result->env) {
        log_debug("PAM environment variables:");
        for (char **p = pam_result->env; *p; p++) {
            log_debug("  %s", *p);
        }
    }
#endif

    struct passwd *pw = getpwnam(username);
    if (!pw) {
        log_error("child_exec: getpwnam failed for user '%s'", username);
        _exit(EXIT_FAILURE);
    }

    /* Build the session environment: count PAM env entries. */
    int n_pam = 0;
    for (char **p = pam_result->env; p && *p; p++)
        n_pam++;

    /* passwd fields + PATH + PAM env entries + NULL terminator */
    char **env = calloc(5 + n_pam + 1, sizeof(*env));
    if (!env) {
        log_syserr("child_exec: calloc");
        _exit(EXIT_FAILURE);
    }

    int i = 0;
    /* getenv() finds the first match, so passwd is authoritative */
    if (asprintf(&env[i++], "USER=%s", pw->pw_name) < 0)
        goto oom;
    if (asprintf(&env[i++], "LOGNAME=%s", pw->pw_name) < 0)
        goto oom;
    if (asprintf(&env[i++], "HOME=%s", pw->pw_dir) < 0)
        goto oom;
    if (asprintf(&env[i++], "SHELL=%s", pw->pw_shell) < 0)
        goto oom;
    env[i++] = "PATH=/usr/local/bin:/usr/bin:/bin";
    for (char **p = pam_result->env; p && *p; p++) {
        env[i++] = *p;
    }
    env[i] = NULL;

    /* Privilege drop: supplementary groups, then gid, then uid.
     * setresgid/setresuid set all three ID slots (real, effective, saved)
     * atomically. */
    if (initgroups(pw->pw_name, pw->pw_gid) < 0) {
        log_syserr("child_exec: initgroups");
        _exit(EXIT_FAILURE);
    }
    if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0) {
        log_syserr("child_exec: setresgid");
        _exit(EXIT_FAILURE);
    }
    if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
        log_syserr("child_exec: setresuid");
        _exit(EXIT_FAILURE);
    }

    /* Defence-in-depth: verify we cannot re-escalate to root. */
    if (setresuid(0, 0, 0) == 0) {
        log_error("CRITICAL: re-escalation to root succeeded after privilege drop");
        _exit(EXIT_FAILURE);
    }

    if (chdir(pw->pw_dir) < 0)
        log_syserr("child_exec: chdir"); /* not fatal, warn only */

    log_debug("exec user session for '%s' with command: %s", username, COMPOSITOR);
    /* Prepend '-' to argv[0] to force a login shell, so we get .profile and PATH. */
    char argv0[64];
    const char *base = strrchr(pw->pw_shell, '/');
    snprintf(argv0, sizeof(argv0), "-%s", base ? base + 1 : pw->pw_shell);
    char *argv[] = {argv0, "-c", COMPOSITOR, NULL};
    execvpe(pw->pw_shell, argv, env);
    log_syserr("child_exec: execvpe");
    _exit(EXIT_FAILURE);

oom:
    log_error("child_exec: out of memory");
    _exit(EXIT_FAILURE);
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
    uid_t greeter_uid = pw->pw_uid;

    char runtime_dir[64];
    snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%u", (unsigned)greeter_uid);

    /* Sync pipe: child blocks reading until parent writes session_id.
     * TODO: replace with IPC channel (ipc_send via parent_end / ipc_recv via child_end). */
    int sync_pipe[2];
    if (pipe2(sync_pipe, O_CLOEXEC) < 0) {
        log_syserr("session_runner: pipe2");
        ipc_close(parent_end);
        ipc_close(child_end);
        _exit(EXIT_FAILURE);
    }

    pid_t greeter_pid = fork();
    if (greeter_pid < 0) {
        log_syserr("session_runner: fork (greeter)");
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        ipc_close(parent_end);
        ipc_close(child_end);
        _exit(EXIT_FAILURE);
    }

    if (greeter_pid == 0) {
        close(sync_pipe[1]);
        ipc_close(parent_end);
        child_exec_greeter(GREETER_USERNAME, s, runtime_dir, sync_pipe[0], child_end);
        /* unreachable */
    }

    /* Parent: close read end of sync pipe; greeter owns child_end. */
    close(sync_pipe[0]);
    ipc_close(child_end);

    /* CreateSession for greeter, using greeter_pid as the session leader. */
    char session_id[32] = {0};
    char session_obj[256] = {0};
    char runtime_path[64] = {0};
    int fifo_fd = -1;

    if (bus_open() < 0) {
        close(sync_pipe[1]);
        waitpid(greeter_pid, NULL, 0);
        ipc_close(parent_end);
        _exit(EXIT_FAILURE);
    }

    if (bus_create_session(s->name, (uint32_t)s->vtnr, greeter_uid, greeter_pid, "", "greeter",
                           session_id, sizeof(session_id), session_obj, sizeof(session_obj),
                           runtime_path, sizeof(runtime_path), &fifo_fd) < 0) {
        close(sync_pipe[1]);
        waitpid(greeter_pid, NULL, 0);
        ipc_close(parent_end);
        bus_close();
        _exit(EXIT_FAILURE);
    }

    log_info("session_runner: greeter session %s started (PID %d) on seat '%s'", session_id,
             (int)greeter_pid, s->name);

    if (s->vtnr > 0 && bus_activate_session(session_obj) < 0) {
        close(sync_pipe[1]);
        waitpid(greeter_pid, NULL, 0);
        close(fifo_fd);
        ipc_close(parent_end);
        bus_close();
        _exit(EXIT_FAILURE);
    }

    /* SHORTCUT: wait to make sure session is active. */
    sleep(1);

    /* Signal greeter child to proceed by writing the session_id. */
    ssize_t nw = write(sync_pipe[1], session_id, strlen(session_id));
    if (nw < 0)
        log_syserr("session_runner: greeter sync pipe write");
    close(sync_pipe[1]);

    /* Wait for greeter to exit. */
    int wstatus = 0;
    pid_t r;
    do {
        r = waitpid(greeter_pid, &wstatus, 0);
    } while (r < 0 && errno == EINTR);

    if (r < 0)
        log_syserr("session_runner: waitpid (greeter)");
    else if (WIFEXITED(wstatus))
        log_info("greeter exited with status %d on seat '%s'", WEXITSTATUS(wstatus), s->name);
    else if (WIFSIGNALED(wstatus))
        log_info("greeter terminated by signal %d (%s) on seat '%s'", WTERMSIG(wstatus),
                 strsignal(WTERMSIG(wstatus)), s->name);

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
    if (s->vtnr > 0) {
        if (asprintf(&env[i++], "XDG_VTNR=%d", s->vtnr) < 0) {
            log_error("session_runner: out of memory");
            _exit(EXIT_FAILURE);
        }
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
        child_exec(username, &pam_result);
        /* unreachable */
    }

    log_info("started user session for '%s' (PID %d) on seat '%s'", username, (int)comp_pid,
             s->name);

    /* Wait for compositor to exit. */
    do {
        r = waitpid(comp_pid, &wstatus, 0);
    } while (r < 0 && errno == EINTR);

    if (r < 0)
        log_syserr("session_runner: waitpid (compositor)");
    else if (WIFEXITED(wstatus))
        log_info("compositor exited with status %d on seat '%s'", WEXITSTATUS(wstatus), s->name);
    else if (WIFSIGNALED(wstatus))
        log_info("compositor terminated by signal %d (%s) on seat '%s'", WTERMSIG(wstatus),
                 strsignal(WTERMSIG(wstatus)), s->name);

    auth_close_session(&pam_result);
    log_debug("session lifecycle complete on seat '%s'", s->name);
    _exit(EXIT_SUCCESS);
}
