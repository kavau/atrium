#include <assert.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "daemon/auth/auth.h"
#include "session.h"

int create_session(const char *username, const char *password, const char *seat,
                   const char *conf_path) {
    assert(username);
    assert(password);
    assert(seat);
    assert(conf_path);

    /* PAM - SHORTCUT: should be done in a dedicated session-helper process */
    auth_result pam_result;
    int r = auth_open_session(username, password, seat, conf_path, &pam_result);
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "Failed to open PAM session: %d\n", r);
        return 1;
    }

    /* SHORTCUT: Give logind some time to activate the session. */
    sleep(2);

    /* Fork the child process for the user session. */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        auth_close_session(&pam_result);
        return 1;
    }

    if (pid == 0) {
        /* This is the child process -- drop privileges and exec the compositor. */
        fprintf(stderr, "Starting child process...\n");

        /* Debug output */
        if (pam_result.env) {
            fprintf(stderr, "PAM environment variables:\n");
            for (char **p = pam_result.env; *p; p++) {
                fprintf(stderr, "  %s\n", *p);
            }
        }

        /* Build the session environment. */
        struct passwd *pw = getpwnam(username);
        if (!pw) {
            fprintf(stderr, "getpwnam failed for user '%s'\n", username);
            _exit(1);
        }

        /* Count PAM env entries. */
        int n_pam = 0;
        for (char **p = pam_result.env; p && *p; p++)
            n_pam++;

        /* passwd fields + PATH + PAM env entries + NULL terminator */
        char **env = calloc(5 + n_pam + 1, sizeof(*env));
        if (!env) {
            perror("calloc");
            _exit(1);
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
        for (char **p = pam_result.env; p && *p; p++) {
            env[i++] = *p;
        }
        env[i] = NULL;

        /* Privilege drop: supplementary groups, then gid, then uid.
         * setresgid/setresuid set all three ID slots (real, effective, saved)
         * atomically. */
        if (initgroups(pw->pw_name, pw->pw_gid) < 0) {
            perror("initgroups");
            _exit(1);
        }
        if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0) {
            perror("setresgid");
            _exit(1);
        }
        if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
            perror("setresuid");
            _exit(1);
        }

        /* Defence-in-depth: verify we cannot re-escalate to root. */
        if (setresuid(0, 0, 0) == 0) {
            fprintf(stderr, "CRITICAL: re-escalation to root succeeded after privilege drop\n");
            _exit(1);
        }

        /* Set the working directory to the user's home. */
        if (chdir(pw->pw_dir) < 0) {
            perror("chdir"); /* not fatal, warn only */
        }

        fprintf(stderr, "Exec user session...\n");
        /* TODO: use execvpe instead, which searches PATH */
        /* TODO: exec the compositor in a login shell */
        execle("/usr/bin/cage", "cage", "foot", NULL, env);
        perror("execle"); /* Error path - a successful exec does not return */
        _exit(1);

    oom:
        fprintf(stderr, "create_session: out of memory\n");
        _exit(1);
    }

    /* This is the parent process -- user session lifetime management. */
    /* SHORTCUT: Wait for the child process to exit. We should monitor SIGCHLD instead. */
    waitpid(pid, NULL, 0);
    fprintf(stderr, "Child process exited, closing PAM handle to end session...\n");
    auth_close_session(&pam_result);

    return 0;
}
