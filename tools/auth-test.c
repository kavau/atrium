/*
 * auth-test.c - test PAM authentication
 *
 * Executes the PAM stack and establishes a session for the given user.  Needs
 * to be run as root with the systemd-run wrapper to escape the existing logind
 * session.
 *
 * The session is kept open to allow inspection with 'loginctl'.
 *
 * Usage:
 *   sudo systemd-run --scope ./build/atrium-auth-test <username> [conf_path]
 *
 * conf_path is the PAM configuration directory (default: /etc/pam.d).
 */

#include <stdio.h>
#include <unistd.h>

#include "daemon/auth/auth.h"

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <username> [conf_path]\n", argv[0]);
        return 1;
    }

    const char *username = argv[1];
    const char *conf_path = argc == 3 ? argv[2] : NULL;
    const char *seat = "seat1";
    const char *password = getpass("Password: ");
    if (!password) {
        fprintf(stderr, "getpass failed\n");
        return 1;
    }

    auth_result result;
    int r = auth_open_session(username, password, seat, conf_path, &result);
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "auth_open_session failed: %d\n", r);
        return 1;
    }

    printf("Authentication successful for user '%s'\n", username);

    if (result.env) {
        printf("PAM environment variables:\n");
        for (char **p = result.env; *p; p++) {
            printf("  %s\n", *p);
        }
    }

    /* Wait for input so we can inspect the session. */
    printf("Press Enter to close the session...\n");
    getchar();

    auth_close_session(&result);
    return 0;
}
