#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/defs.h"
#include "lib/log.h"
#include "seat.h"
#include "session.h"
#include "vt.h"

/* SHORCUT: needed to pass hardcoded seat configs */
typedef struct {
    const char *seat_name;
    const char *username;
    const char *password;
} seat_config;

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    log_info("starting");
    log_debug("debug logging enabled");

    /* Allocate a VT for seat0. SHORTCUT: needs to be done after seat0 is discovered. */
    int vtnr = vt_alloc();
    if (vtnr < 0) {
        log_error("failed to allocate VT for seat0: %d", vtnr);
        return EXIT_FAILURE;
    }
    /* TODO: suppress VT keyboard so keystrokes typed into the Wayland
    session don't leak into the TTY's input buffer. */

    /* SHORTCUT: create user sessions for seat0 and seat1 with hardcoded
    parameters. We add seats in reverse order here because seat_add appends to
    the front (quite messy, but this code is just temporary). */
    seat_config configs[] = {SEAT_CONFIGS};
    int n_seats = sizeof(configs) / sizeof(configs[0]);
    for (int i = n_seats - 1; i >= 0; i--) {
        log_info("adding seat '%s'", configs[i].seat_name);
        if (!seat_add(configs[i].seat_name, strcmp(configs[i].seat_name, "seat0") ? 0 : vtnr)) {
            log_error("failed to add seat '%s'", configs[i].seat_name);
        }
    }

    int i = 0; /* SHORTCUT: using hardcoded session parameters */
    for (seat *s = seat_first(); s; s = seat_next(s), ++i) {
        log_info("starting session for %s on seat '%s'", configs[i].username, s->name);
        int r = session_start(configs[i].username, configs[i].password, PAM_CONF_PATH, s);
        if (r != 0) {
            log_error("failed to create session for %s on %s: %d", configs[i].username, s->name, r);
        }
        sleep(2); /* SHORTCUT: wait a bit before starting the next session */
    }

    sleep(360); /* SHORTCUT: keep the sessions alive for 6 minutes for testing */

    /* Cleanup */
    if (vtnr > 0) {
        vt_release(vtnr);
    }
    return EXIT_SUCCESS;
}
