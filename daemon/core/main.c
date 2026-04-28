#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "seat.h"
#include "session.h"
#include "vt.h"

#define SEAT0_USER "alice"
#define SEAT0_PASSWORD "password123"
#define SEAT1_USER "bob"
#define SEAT1_PASSWORD "password456"
#define CONF_PATH "data/pam"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Allocate a VT for seat0. SHORTCUT: needs to be done after seat0 is discovered. */
    const char *dummy_seat = "seat0"; /* SHORTCUT */
    int vtnr = 0;
    if (strcmp(dummy_seat, "seat0") == 0) {
        vtnr = vt_alloc();
        if (vtnr < 0) {
            fprintf(stderr, "Failed to allocate VT: %d\n", vtnr);
            return EXIT_FAILURE;
        }
        /* TODO: suppress VT keyboard so keystrokes typed into the Wayland
        session don't leak into the TTY's input buffer. */
    }

    /* SHORTCUT: create user sessions for seat0 and seat1 with hardcoded
    parameters, then periodically scan whether the sessions are still running,
    and tear them down if not. */

    /* Note that this does not work in its current form - PAM must be called in
    a child process since it pollutes the environment with session-specific
    variables. We must fork a session helper for each seat, and call
    session_start() from the helper. It is also the helper's responsibility to
    call session_stop() after the compositor exits.*/

    fprintf(stderr, "Starting session for %s on seat0...\n", SEAT0_USER);
    seat seat0 = {
        .name = "seat0",
        .vtnr = vtnr,
    };
    int r = session_start(SEAT0_USER, SEAT0_PASSWORD, CONF_PATH, &seat0);
    if (r != 0) {
        fprintf(stderr, "Failed to create session for %s on %s: %d\n", SEAT0_USER, seat0.name, r);
        goto err;
    }

    sleep(5); /* SHORTCUT: wait a bit before starting the next session */

    fprintf(stderr, "Starting session for %s on seat1...\n", SEAT1_USER);
    seat seat1 = {
        .name = "seat1",
        .vtnr = 0,
    };
    r = session_start(SEAT1_USER, SEAT1_PASSWORD, CONF_PATH, &seat1);
    if (r != 0) {
        fprintf(stderr, "Failed to create session for %s on %s: %d\n", SEAT1_USER, seat1.name, r);
        goto err;
    }

    sleep(360); /* SHORTCUT: keep the sessions alive for 6 minutes for testing */

err:
    if (vtnr > 0) {
        vt_release(vtnr);
    }
    return r;
}
