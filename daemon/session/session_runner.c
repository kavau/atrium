#include <assert.h>
#include <grp.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "auth.h"
#include "daemon/core/seat.h"
#include "daemon/core/vt.h"
#include "lib/defs.h"
#include "lib/log.h"
#include "session_runner.h"

/* Configure the environment, drop privileges, and exec the greeter or user
session. Called from the child side of the fork. This function never returns. */
static _Noreturn void child_exec(const char *username, const auth_result *pam_result,
                                 session_type type) {
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
        case SESSION_GREETER:
            log_debug("exec greeter session");
            execle("/bin/sh", "sh", "-c", GREETER, NULL, env);
            break;
        case SESSION_USER:
            log_debug("exec user session for '%s'", username);
            /* TODO: use execvpe instead, which searches PATH */
            /* TODO: exec the compositor in a login shell */
            execle("/bin/sh", "sh", "-c", COMPOSITOR, NULL, env);
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
                              const seat *s, session_type type) {
    assert(username);
    assert(password);
    assert(s);
    assert(type == SESSION_GREETER || type == SESSION_USER);

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
    const char *pam_service_name = type == SESSION_GREETER ? "atrium-greeter" : "atrium";
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
        child_exec(username, &pam_result, type);
    }

    /* This is the parent process -- wait for the compositor to exit, then call
    auth_close_session() and exit. */
    const char *session_desc = type == SESSION_GREETER ? "greeter" : "user";
    log_info("started %s session child with PID %d on seat '%s'", session_desc, pid, s->name);

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
