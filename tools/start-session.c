/*
 * start-session.c - starts a user session on a given seat. Intended for testing
 * and development.
 *
 * Usage (with local PAM configuration):
 *     sudo systemd-run --scope ./build/atrium-start-session <username> <seat> data/pam
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "daemon/core/seat.h"
#include "daemon/core/session.h"
#include "daemon/core/vt.h"

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s <username> <seat> [pam_conf_path]\n", argv[0]);
        return 1;
    }

    const char *username = argv[1];
    seat s = {.vtnr = 0};
    snprintf(s.name, sizeof(s.name), "%s", argv[2]);
    const char *pam_conf_path = argc == 4 ? argv[3] : NULL;

    const char *password = getpass("Password: ");
    if (!password) {
        fprintf(stderr, "getpass failed\n");
        return 1;
    }

    /* Allocate a VT for seat0. */
    if (strcmp(s.name, "seat0") == 0) {
        s.vtnr = vt_alloc();
        if (s.vtnr < 0) {
            fprintf(stderr, "Failed to allocate VT: %d\n", s.vtnr);
            return EXIT_FAILURE;
        }
        /* TODO: suppress VT keyboard so keystrokes typed into the Wayland
        session don't leak into the TTY's input buffer. */
    }

    int r = session_start(username, password, pam_conf_path, &s);
    if (r != 0) {
        fprintf(stderr, "Failed to start session: %d\n", r);
        if (s.vtnr > 0) {
            vt_release(s.vtnr);
        }
        return EXIT_FAILURE;
    }

    /* SHORTCUT: Wait for input so we can inspect the session. Instead we should
    monitor the child PID and call session_stop() when it exits. */
    printf("Press Enter to exit the program...\n");
    getchar();

    if (s.vtnr > 0) {
        vt_release(s.vtnr);
    }
    return r;
}
