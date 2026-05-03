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
    parameters. */
    seat_config configs[] = {SEAT_CONFIGS};
    int n_seats = sizeof(configs) / sizeof(configs[0]);
    for (int i = 0; i < n_seats; i++) {
        log_info("starting session for %s on seat '%s'", configs[i].username, configs[i].seat_name);
        seat s = {
            .vtnr = strcmp(configs[i].seat_name, "seat0") ? 0 : vtnr,
        };
        snprintf(s.name, sizeof(s.name), "%s", configs[i].seat_name);
        int r = session_start(configs[i].username, configs[i].password, PAM_CONF_PATH, &s);
        if (r != 0) {
            log_error("failed to create session for %s on %s: %d", configs[i].username, s.name, r);
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
