/*
 * auth-test.c — standalone PAM authentication test for atrium.
 *
 * Exercises the full auth_begin() / auth_open_session() / auth_close()
 * sequence against the real PAM stack with real credentials.  The purpose is
 * to validate src/auth.c in complete isolation — before it touches the
 * daemon — so that any failures can be diagnosed without compositor, D-Bus,
 * or VT involvement.
 *
 * Usage:
 *   sudo ./build/atrium-auth-test <username>
 *
 * The binary prompts for a password via getpass(3) (which reads directly from
 * /dev/tty so the prompt appears even when stdout is redirected), calls
 * auth_begin(), auth_open_session(), prints the full result, then calls
 * auth_close().
 *
 * Must be run as root: pam_open_session writes to the audit log and updates
 * /proc/self/loginuid, both of which require CAP_AUDIT_WRITE / CAP_SYS_ADMIN.
 *
 * Edge cases to exercise manually:
 *   correct password   → prints uid, gid, PAM env, exits 0
 *   wrong password     → prints PAM error, exits 1
 *   non-existent user  → pam_authenticate fails, exits 1
 *
 * _GNU_SOURCE is injected by the build system; do not redefine it here.
 * getpass(3) requires _GNU_SOURCE (or _XOPEN_SOURCE >= 500).
 */

#include <stdio.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include "auth.h"
#include "log.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <username>\n", argv[0]);
        return 1;
    }

    const char *username = argv[1];

    /*
     * getpass() writes the prompt to /dev/tty and returns a pointer into a
     * static buffer.  It is intentionally used here because this is a manual
     * validation tool, not production code.  The daemon (Phase 10) receives
     * credentials via a pipe from the greeter.
     */
    const char *password = getpass("Password: ");
    if (!password) {
        fprintf(stderr, "%s: getpass failed\n", argv[0]);
        return 1;
    }

    auth_result result;
    int r = auth_begin(username, password, &result);
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "%s: authentication failed (PAM error %d)\n",
                argv[0], r);
        return 1;
    }

    printf("auth_begin: OK\n");
    printf("  uid     = %u\n", (unsigned)result.uid);
    printf("  gid     = %u\n", (unsigned)result.gid);

    r = auth_open_session(&result);
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "%s: auth_open_session failed (PAM error %d)\n",
                argv[0], r);
        auth_close(&result);
        return 1;
    }

    printf("auth_open_session: OK\n");
    printf("  pam_env:\n");
    if (result.pam_env) {
        for (char **e = result.pam_env; *e; e++)
            printf("    %s\n", *e);
    } else {
        printf("    (none)\n");
    }

    auth_close(&result);
    printf("auth_close: OK\n");
    return 0;
}
