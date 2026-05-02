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
#include "lib/log.h"
#include "session_runner.h"

/* Configure the environment, drop privileges, and exec the user session. Called
from the child side of the fork. This function never returns. */
static _Noreturn void child_exec(const char *username, const auth_result *pam_result) {
    /* Debug output */
    if (pam_result->env) {
        log_debug("PAM environment variables:");
        for (char **p = pam_result->env; *p; p++) {
            log_debug("  %s", *p);
        }
    }

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

    log_debug("exec user session");
    /* TODO: use execvpe instead, which searches PATH */
    /* TODO: exec the compositor in a login shell */
    execle("/usr/bin/cage", "cage", "-s", "-m", "last", "--", "foot", "-f", "monospace:size=18",
           "-o", "colors-dark.background=000000", NULL, env);
    log_syserr("child_exec: execle"); /* Error path - a successful exec does not return */
    _exit(EXIT_FAILURE);

oom:
    log_error("child_exec: out of memory");
    _exit(EXIT_FAILURE);
}

_Noreturn void session_runner(const char *username, const char *password, const char *conf_path,
                              const seat *s) {
    /* Build the PAM environment */
    char **env = calloc(5, sizeof(*env));
    if (!env) {
        log_syserr("session_runner: calloc");
        _exit(EXIT_FAILURE);
    }
    int i = 0;
    if (asprintf(&env[i++], "XDG_SEAT=%s", s->name) < 0) {
        log_error("session_runner: out of memory");
        _exit(EXIT_FAILURE);
    }
    if (asprintf(&env[i++], "XDG_VTNR=%d", s->vtnr) < 0) {
        log_error("session_runner: out of memory");
        _exit(EXIT_FAILURE);
    }
    env[i++] = "XDG_SESSION_TYPE=wayland";
    env[i++] = "XDG_SESSION_CLASS=user"; /* TODO: must be "greeter" for a greeter session */
    env[i] = NULL;

    /* Authenticate with PAM - also establishes the logind session */
    auth_result pam_result;
    int r = auth_open_session(username, password, (const char **)env, conf_path, &pam_result);
    free(env[0]);
    free(env[1]);
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
        child_exec(username, &pam_result);
    }

    /* This is the parent process -- wait for the compositor to exit, then call
    auth_close_session() and exit. */
    log_info("started session child with PID %d on seat '%s'", pid, s->name);

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        log_info("compositor exited with exit status %d on seat '%s'", WEXITSTATUS(status),
                 s->name);
    } else if (WIFSIGNALED(status)) {
        log_info("compositor terminated with signal %d (%s) on seat '%s'", WTERMSIG(status),
                 strsignal(WTERMSIG(status)), s->name);
    } else {
        log_warn("compositor exited with unexpected status %d on seat '%s'", status, s->name);
    }

    auth_close_session(&pam_result);
    log_debug("closed login session on seat '%s'", s->name);
    _exit(EXIT_SUCCESS);
}
