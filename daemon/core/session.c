#include <assert.h>
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

        if (pam_result.env) {
            printf("PAM environment variables:\n");
            for (char **p = pam_result.env; *p; p++) {
                printf("  %s\n", *p);
            }
        }

        /* Build the session environment. */
        struct passwd *pw = getpwnam(username);
        if (!pw) {
            fprintf(stderr, "getpwnam failed for user '%s'\n", username);
            return 1;
        }

        /* Count PAM env entries. */
        int n_pam = 0;
        for (char **p = pam_result.env; p && *p; p++)
            n_pam++;

        /* passwd fields + PATH + PAM env entries + NULL terminator */
        char **env = calloc(5 + n_pam + 1, sizeof(*env));
        if (!env) {
            perror("calloc");
            return 1;
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

        /* TODO:
         * - privilege drop to the user account
         * - chdir to the user home directory
         * - exec the compositor in a login shell
         */

        fprintf(stderr, "Exec user session...\n");
        /* TODO: use execvpe instead, which searches PATH */
        execle("/usr/bin/cage", "cage", "foot", NULL, env);
        perror("execl"); /* Error path - a successful exec does not return */
        return 1;

    oom:
        fprintf(stderr, "create_session: out of memory\n");
        for (int j = 0; j < i - 1; j++) {
            free(env[j]);
        }
        free(env);
        return 1;
    }

    /* This is the parent process -- user session lifetime management. */
    /* SHORTCUT: Wait for the child process to exit. We should monitor SIGCHLD instead. */
    waitpid(pid, NULL, 0);
    fprintf(stderr, "Child process exited, closing PAM handle to end session...\n");
    auth_close_session(&pam_result);

    return 0;
}
