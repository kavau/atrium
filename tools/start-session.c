/*
 * start-session.c - starts the full session lifecycle (greeter + user session)
 * on a given seat. Intended for testing and development.
 *
 * Usage (with local PAM configuration):
 *     sudo systemd-run --scope ./build/atrium-start-session <seat> data/pam
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daemon/core/seat.h"
#include "daemon/core/vt.h"
#include "daemon/session/session_runner.h"
#include "lib/defs.h"

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <seat> [pam_conf_path]\n", argv[0]);
        return 1;
    }

    seat s = {.vtnr = 0, .state = 0, .runner_pid = 0};
    snprintf(s.name, sizeof(s.name), "%s", argv[1]);
    const char *pam_conf_path = argc == 3 ? argv[2] : PAM_CONF_PATH;

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

    /* Run the full session lifecycle (greeter -> user session) and exit. */
    session_runner(pam_conf_path, &s);
}
