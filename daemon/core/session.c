#include <assert.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "daemon/auth/auth.h"
#include "seat.h"
#include "session.h"
#include "vt.h"

/* Configure the environment, drop privileges, and exec the user session. Called
from the child side of the fork. This function never returns. */
static _Noreturn void child_exec(const char *username, const auth_result *pam_result) {
    /* Debug output */
    if (pam_result->env) {
        fprintf(stderr, "PAM environment variables:\n");
        for (char **p = pam_result->env; *p; p++) {
            fprintf(stderr, "  %s\n", *p);
        }
    }

    /* Build the session environment. */
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "getpwnam failed for user '%s'\n", username);
        _exit(EXIT_FAILURE);
    }

    /* Count PAM env entries. */
    int n_pam = 0;
    for (char **p = pam_result->env; p && *p; p++)
        n_pam++;

    /* passwd fields + PATH + PAM env entries + NULL terminator */
    char **env = calloc(5 + n_pam + 1, sizeof(*env));
    if (!env) {
        perror("calloc");
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
        perror("initgroups");
        _exit(EXIT_FAILURE);
    }
    if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0) {
        perror("setresgid");
        _exit(EXIT_FAILURE);
    }
    if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
        perror("setresuid");
        _exit(EXIT_FAILURE);
    }

    /* Defence-in-depth: verify we cannot re-escalate to root. */
    if (setresuid(0, 0, 0) == 0) {
        fprintf(stderr, "CRITICAL: re-escalation to root succeeded after privilege drop\n");
        _exit(EXIT_FAILURE);
    }

    /* Set the working directory to the user's home. */
    if (chdir(pw->pw_dir) < 0) {
        perror("chdir"); /* not fatal, warn only */
    }

    fprintf(stderr, "Exec user session...\n");
    /* TODO: use execvpe instead, which searches PATH */
    /* TODO: exec the compositor in a login shell */
    execle("/usr/bin/cage", "cage", "-s", "-m", "last", "--", "foot", "-f", "monospace:size=18",
           "-o", "colors-dark.background=000000", NULL, env);
    perror("execle"); /* Error path - a successful exec does not return */
    _exit(EXIT_FAILURE);

oom:
    fprintf(stderr, "child_exec: out of memory\n");
    _exit(EXIT_FAILURE);
}

/* Create the logind session and launch the compositor child process. This
function is itself executed as a child process of the daemon - the session
runner. It exits when the session has completed. */
static _Noreturn void session_runner(const char *username, const char *password,
                                     const char *conf_path, seat *s) {
    /* Build the PAM environment */
    char **env = calloc(5, sizeof(*env));
    if (!env) {
        perror("calloc");
        _exit(EXIT_FAILURE);
    }
    int i = 0;
    if (asprintf(&env[i++], "XDG_SEAT=%s", s->name) < 0) {
        fprintf(stderr, "session_runner: out of memory\n");
        _exit(EXIT_FAILURE);
    }
    if (asprintf(&env[i++], "XDG_VTNR=%d", s->vtnr) < 0) {
        fprintf(stderr, "session_runner: out of memory\n");
        _exit(EXIT_FAILURE);
    }
    env[i++] = "XDG_SESSION_TYPE=wayland";
    env[i++] = "XDG_SESSION_CLASS=user"; /* TODO: must be "greeter" for a greeter session */
    env[i] = NULL;

    /* Authenticate with PAM - also establishes the logind session */
    int r = auth_open_session(username, password, (const char **)env, conf_path, &s->pam_result);
    free(env[0]);
    free(env[1]);
    free(env);
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "Failed to open PAM session: %d\n", r);
        _exit(EXIT_FAILURE);
    }

    /* Activate the allocated VT (seat0 only; blocks until the VT is active) */
    if (s->vtnr > 0 && vt_activate(s->vtnr) < 0) {
        fprintf(stderr, "Failed to activate VT%d\n", s->vtnr);
        auth_close_session(&s->pam_result);
        _exit(EXIT_FAILURE);
    }

    /* Fork the child process for the user session. */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        auth_close_session(&s->pam_result);
        _exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        /* This is the child process -- drop privileges and exec the compositor. */
        fprintf(stderr, "Starting child process...\n");
        child_exec(username, &s->pam_result);
    }

    /* This is the parent process -- wait for the compositor to exit, then call
    auth_close_session() and exit. */
    fprintf(stderr, "Started session child with PID %d for user '%s' on seat '%s'\n", pid, username,
            s->name);
    sleep(300); /* SHORTCUT: session lifecycle management is not yet implemented. */
    auth_close_session(&s->pam_result);
    _exit(EXIT_SUCCESS);
}

int session_start(const char *username, const char *password, const char *conf_path, seat *s) {
    assert(username);
    assert(password);
    assert(s);

    /* Fork the session runner */
    pid_t runner_pid = fork();
    if (runner_pid < 0) {
        perror("fork");
        return 1;
    }

    if (runner_pid == 0) {
        /* This is the child process -- run the session setup and exec. */
        fprintf(stderr, "Starting session runner...\n");
        session_runner(username, password, conf_path, s);
    }

    /* This is the parent process -- return to the main loop. */
    fprintf(stderr, "Started session runner with PID %d for user '%s' on seat '%s'\n", runner_pid,
            username, s->name);
    return 0;
}
