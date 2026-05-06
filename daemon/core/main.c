#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "greeter.h"
#include "lib/defs.h"
#include "lib/log.h"
#include "seat.h"
#include "session.h"
#include "vt.h"

/* SHORTCUT: needed to pass hardcoded seat configs */
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

    /* SHORTCUT: allow hardware initialization to complete before seat discovery */
    sleep(SEAT_DISCOVERY_DELAY);

    /* Allocate a VT for seat0. SHORTCUT: needs to be done after seat0 is discovered. */
#if HEADLESS
    log_info("headless mode enabled, skipping VT allocation");
    int vtnr = 0;
#else
    int vtnr = vt_alloc();
    if (vtnr < 0) {
        log_error("failed to allocate VT for seat0: %d", vtnr);
        return EXIT_FAILURE;
    }
    /* TODO: suppress VT keyboard so keystrokes typed into the Wayland
    session don't leak into the TTY's input buffer. */
#endif

    /* SHORTCUT: create hardcoded seats. We add seats in reverse order because
    seat_add appends to the front; this keeps the hardcoded session parameters
    in sync with the seat list (quite messy, but this code is just temporary).
    */
    seat_config configs[] = {SEAT_CONFIGS};
    int n_seats = sizeof(configs) / sizeof(configs[0]);
    for (int i = n_seats - 1; i >= 0; i--) {
        log_info("adding seat '%s'", configs[i].seat_name);
        if (!seat_add(configs[i].seat_name, strcmp(configs[i].seat_name, "seat0") ? 0 : vtnr)) {
            log_error("failed to add seat '%s'", configs[i].seat_name);
        }
    }

    /* Start a greeter on each seat */
    for (seat *s = seat_first(); s; s = seat_next(s)) {
        log_info("starting greeter on seat '%s'", s->name);
        int r = greeter_start(PAM_CONF_PATH, s);
        if (r != 0) {
            log_error("failed to launch greeter on %s: %d", s->name, r);
            /* TODO: retry */
        }
        sleep(5); /* SHORTCUT: wait a bit before starting the next greeter */
    }

    /* Simple event loop */
    while (1) {
        int wstatus;
        pid_t pid = waitpid(-1, &wstatus, 0);
        if (pid < 0) {
            if (errno == EINTR)
                continue;
            log_syserr("waitpid");
            break; /* no children remain */
        }

        int i = 0; /* SHORTCUT: using hardcoded session parameters */
        for (seat *s = seat_first(); s; s = seat_next(s), ++i) {
            if (pid == s->runner_pid) {
                log_debug("session runner with PID %d on seat '%s' (state %d) exited", pid, s->name,
                          s->state);
                s->runner_pid = 0;

                switch (s->state) {
                    default:
                        log_error("invalid seat state %d on seat '%s'", s->state, s->name);
                        /* falls through - start greeter */
                    case SEAT_IDLE:
                        if (s->state == SEAT_IDLE)
                            log_warn(
                                "session runner on seat '%s' exited without starting a session",
                                s->name);
                        /* falls through - start greeter */
                    case SEAT_SESSION: {
                        if (s->state == SEAT_SESSION)
                            log_info("session on seat '%s' terminated, restarting greeter",
                                     s->name);
                        sleep(1); /* SHORTCUT: avoid tight crash-loop */
                        int r = greeter_start(PAM_CONF_PATH, s);
                        if (r != 0) {
                            log_error("failed to restart greeter on %s: %d", s->name, r);
                            s->state = SEAT_IDLE; /* TODO: retry */
                        }
                        break;
                    }
                    case SEAT_GREETER: {
                        /* SHORTCUT: start session only after successful auth */
                        log_info("greeter on seat '%s' terminated, starting user session", s->name);
                        sleep(1); /* SHORTCUT: avoid tight crash-loop */
                        int r = session_start(configs[i].username, configs[i].password,
                                              PAM_CONF_PATH, s);
                        if (r != 0) {
                            log_error("failed to create session for %s on %s: %d",
                                      configs[i].username, s->name, r);
                            s->state = SEAT_IDLE; /* TODO: retry */
                        }
                        break;
                    }
                }
            }
        }
    }

    /* Cleanup */
    if (vtnr > 0) {
        vt_release(vtnr);
    }
    return EXIT_SUCCESS;
}
