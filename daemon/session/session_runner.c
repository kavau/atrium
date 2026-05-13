#include <assert.h>
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

/* Configure the environment, drop privileges, and exec the greeter. Blocks on
 * sync_read_fd until the parent writes the session_id after CreateSession
 * completes.
 * Called from the child side of the fork. This function never returns. */
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

    /* USER LOGNAME PATH XDG_SEAT [XDG_VTNR] XDG_SESSION_TYPE XDG_SESSION_CLASS
     * XDG_RUNTIME_DIR XDG_SESSION_ID WLR_LIBINPUT_NO_DEVICES NULL */
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

    char cmd[MAX_LEN_GREETER_CMD];
    if (ch) {
        if (ipc_prepare_for_exec(ch) < 0) {
            log_syserr("child_exec_greeter: ipc_prepare_for_exec");
            _exit(EXIT_FAILURE);
        }
        char fd_args[MAX_LEN_GREETER_ARGS];
        ipc_fmt_args(ch, fd_args, sizeof(fd_args));
        snprintf(cmd, sizeof(cmd), "%s %s", GREETER, fd_args);
    } else {
        snprintf(cmd, sizeof(cmd), "%s", GREETER);
    }

    log_debug("child_exec_greeter: exec: %s", cmd);
    /* TODO: running the greeter in a shell is unnecessary */
    execle("/bin/sh", "sh", "-c", cmd, NULL, env);
    log_syserr("child_exec_greeter: execle");
    _exit(EXIT_FAILURE);

oom:
    log_error("child_exec_greeter: out of memory");
    _exit(EXIT_FAILURE);
}

/* Configure the environment, drop privileges, and exec the greeter or user
session. Called from the child side of the fork. This function never returns. */
static _Noreturn void child_exec(const char *username, const auth_result *pam_result,
                                 session_type type, ipc_channel *ch) {
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

    /* Build the session environment. */
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        log_error("child_exec: getpwnam failed for user '%s'", username);
        _exit(EXIT_FAILURE);
    }

    /* Count PAM env entries. */
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

    /* Set the working directory to the user's home. */
    if (chdir(pw->pw_dir) < 0) {
        log_syserr("child_exec: chdir"); /* not fatal, warn only */
    }

    switch (type) {
        case SESSION_GREETER: {
            char cmd[MAX_LEN_GREETER_CMD];
            if (ch) {
                /* Pass IPC channel file descriptors to the greeter */
                if (ipc_prepare_for_exec(ch) < 0) {
                    log_syserr("child_exec: ipc_prepare_for_exec");
                    _exit(EXIT_FAILURE);
                }
                char fd_args[MAX_LEN_GREETER_ARGS];
                ipc_fmt_args(ch, fd_args, sizeof(fd_args));
                snprintf(cmd, sizeof(cmd), "%s %s", GREETER, fd_args);
            } else {
                snprintf(cmd, sizeof(cmd), "%s", GREETER);
            }
            log_debug("exec greeter with command: %s", cmd);
            /* TODO: running the greeter in a shell is unnecessary */
            execle("/bin/sh", "sh", "-c", cmd, NULL, env);
            break;
        }
        case SESSION_USER:
            log_debug("exec user session for '%s' with command: %s", username, COMPOSITOR);
            /* Prepend '-' to argv[0] to force a login shell, so we get .profile and PATH. */
            char argv0[MAX_LEN_SHELL_NAME];
            const char *base = strrchr(pw->pw_shell, '/');
            snprintf(argv0, sizeof(argv0), "-%s", base ? base + 1 : pw->pw_shell);
            char *argv[] = {argv0, "-c", COMPOSITOR, NULL};
            execvpe(pw->pw_shell, argv, env);
            break;
        default:
            log_error("child_exec: invalid session type %d", type);
            _exit(EXIT_FAILURE);
    }

    log_syserr("child_exec: execle"); /* Error path - a successful exec does not return */
    _exit(EXIT_FAILURE);

oom:
    log_error("child_exec: out of memory");
    _exit(EXIT_FAILURE);
}

_Noreturn void session_runner(const char *username, const char *password, const char *pam_conf_path,
                              const seat *s, session_type type, ipc_channel *ch) {
    assert(username);
    assert(password);
    assert(s);
    assert(type == SESSION_GREETER || type == SESSION_USER);

    if (type == SESSION_GREETER) {
        /* Greeter session via direct D-Bus CreateSession (no PAM). Fork first
         * so we can pass the child's PID to CreateSession. */
        struct passwd *pw = getpwnam(username);
        if (!pw) {
            log_error("session_runner: getpwnam failed for '%s'", username);
            _exit(EXIT_FAILURE);
        }
        uid_t greeter_uid = pw->pw_uid;

        /* Derive runtime dir; logind always uses /run/user/<uid>. */
        char runtime_dir[64];
        snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%u", (unsigned)greeter_uid);

        /* Sync pipe: child blocks reading until parent writes session_id.
         * TODO: once session_runner owns the IPC channel, reuse parent_end for
         * this sync and drop the sync pipe. */
        int sync_pipe[2];
        if (pipe2(sync_pipe, O_CLOEXEC) < 0) {
            log_syserr("session_runner: pipe2");
            _exit(EXIT_FAILURE);
        }

        pid_t pid = fork();
        if (pid < 0) {
            log_syserr("session_runner: fork");
            close(sync_pipe[0]);
            close(sync_pipe[1]);
            _exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            close(sync_pipe[1]);
            child_exec_greeter(username, s, runtime_dir, sync_pipe[0], ch);
            /* unreachable */
        }

        /* Parent: create the logind session with the child's PID as leader. */
        close(sync_pipe[0]);

        char session_id[32] = {0};
        char session_obj[256] = {0};
        char runtime_path[64] = {0};
        int fifo_fd = -1;

        if (bus_open() < 0) {
            close(sync_pipe[1]); /* EOF -> child bails out */
            waitpid(pid, NULL, 0);
            _exit(EXIT_FAILURE);
        }

        if (bus_create_session(s->name, (uint32_t)s->vtnr, greeter_uid, pid, "", "greeter",
                               session_id, sizeof(session_id), session_obj, sizeof(session_obj),
                               runtime_path, sizeof(runtime_path), &fifo_fd) < 0) {
            close(sync_pipe[1]);
            waitpid(pid, NULL, 0);
            bus_close();
            _exit(EXIT_FAILURE);
        }

        log_info("session_runner: greeter session %s started (PID %d) on seat '%s'", session_id,
                 (int)pid, s->name);

        if (s->vtnr > 0 && bus_activate_session(session_obj) < 0) {
            close(sync_pipe[1]);
            waitpid(pid, NULL, 0);
            close(fifo_fd);
            bus_close();
            _exit(EXIT_FAILURE);
        }

        /* SHORTCUT: wait to make sure session is active */
        sleep(1);

        /* Signal child to proceed by writing the session_id. */
        ssize_t nw = write(sync_pipe[1], session_id, strlen(session_id));
        if (nw < 0)
            log_syserr("session_runner: greeter sync pipe write");
        close(sync_pipe[1]);

        int wstatus = 0;
        int r;
        do {
            r = waitpid(pid, &wstatus, 0);
        } while (r < 0 && errno == EINTR);

        if (r < 0)
            log_syserr("session_runner: waitpid");
        else if (WIFEXITED(wstatus))
            log_info("greeter exited with status %d on seat '%s'", WEXITSTATUS(wstatus), s->name);
        else if (WIFSIGNALED(wstatus))
            log_info("greeter terminated with signal %d (%s) on seat '%s'", WTERMSIG(wstatus),
                     strsignal(WTERMSIG(wstatus)), s->name);

        close(fifo_fd); /* signals logind that the greeter session has ended */
        bus_close();
        _exit(EXIT_SUCCESS);
    }

    /* SESSION_USER: authenticate via PAM. */

    /* Build the PAM environment */
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
    env[i++] = type == SESSION_GREETER ? "XDG_SESSION_CLASS=greeter" : "XDG_SESSION_CLASS=user";
    env[i++] = NULL;
    assert(i == n_env);

    /* Authenticate with PAM - also establishes the logind session */
    const char *pam_service_name = type == SESSION_GREETER ? "atrium-greeter-dev" : "atrium-dev";
    auth_result pam_result;
    int r = auth_open_session(username, password, (const char **)env, pam_conf_path,
                              pam_service_name, &pam_result);

    free(env[0]); /* XDG_SEAT */
    if (s->vtnr > 0)
        free(env[1]); /* XDG_VTNR */
    free(env);

    if (r != PAM_SUCCESS) {
        log_error("failed to open PAM session: %d", r);
        _exit(EXIT_FAILURE);
    }

    /* Activate the allocated VT (seat0 only; blocks until the VT is active) */
    if (s->vtnr > 0 && vt_activate(s->vtnr) < 0) {
        log_error("failed to activate VT%d", s->vtnr);
        auth_close_session(&pam_result);
        _exit(EXIT_FAILURE);
    }

    /* Fork the child process for the user session. */
    pid_t pid = fork();
    if (pid < 0) {
        log_syserr("session_runner: fork");
        auth_close_session(&pam_result);
        _exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        /* This is the child process -- drop privileges and exec the compositor. */
        log_debug("session_runner: starting child process");
        child_exec(username, &pam_result, type, ch);
    }

    /* This is the parent process -- wait for the compositor to exit, then call
    auth_close_session() and exit. */
    const char *session_desc = type == SESSION_GREETER ? "greeter" : "user";
    log_info("started %s session child with PID %d on seat '%s'", session_desc, pid, s->name);
    if (ch) {
        ipc_close(ch); /* only used by child */
    }

    int wstatus = 0;
    do {
        r = waitpid(pid, &wstatus, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        log_syserr("session_runner: waitpid");
        auth_close_session(&pam_result);
        _exit(EXIT_FAILURE);
    }

    const char *child_desc = type == SESSION_GREETER ? "greeter" : "compositor";
    if (WIFEXITED(wstatus)) {
        log_info("%s exited with exit status %d on seat '%s'", child_desc, WEXITSTATUS(wstatus),
                 s->name);
    } else if (WIFSIGNALED(wstatus)) {
        log_info("%s terminated with signal %d (%s) on seat '%s'", child_desc, WTERMSIG(wstatus),
                 strsignal(WTERMSIG(wstatus)), s->name);
    } else {
        log_warn("%s exited with unexpected status %d on seat '%s'", child_desc, wstatus, s->name);
    }

    auth_close_session(&pam_result);
    log_debug("closed %s session on seat '%s'", session_desc, s->name);
    _exit(EXIT_SUCCESS);
}
