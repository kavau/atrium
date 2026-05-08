#include <assert.h>
#include <errno.h>
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

/* Parses credential string returned by the greeter (format: "username\0password\0") */
static int parse_credentials(char *buf, ssize_t n, const char **username, const char **password) {
    if (n <= 0)
        return -1;

    *username = buf;
    size_t ulen = strnlen(buf, (size_t)n);
    if (ulen >= (size_t)n)
        return -1;

    *password = buf + ulen + 1;
    size_t remaining = (size_t)n - ulen - 1;
    if (strnlen(*password, remaining) >= remaining)
        return -1;

    return 0;
}

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
    seat_add appends to the front (although order does not matter) */
    char *seat_names[] = {SEATS};
    int n_seats = sizeof(seat_names) / sizeof(seat_names[0]);
    for (int i = n_seats - 1; i >= 0; i--) {
        log_info("adding seat '%s'", seat_names[i]);
        if (!seat_add(seat_names[i], strcmp(seat_names[i], "seat0") ? 0 : vtnr)) {
            log_error("failed to add seat '%s'", seat_names[i]);
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

        for (seat *s = seat_first(); s; s = seat_next(s)) {
            if (pid == s->runner_pid) {
                log_debug("session runner with PID %d on seat '%s' (state %d) exited", pid, s->name,
                          s->state);
                s->runner_pid = 0;

                switch (s->state) {
                    default:
                        log_error("invalid seat state %d on seat '%s'", s->state, s->name);
                        goto restart_greeter;
                    case SEAT_IDLE:
                        log_warn("seat '%s' is idle but had an active session runner", s->name);
                        goto restart_greeter;
                    case SEAT_GREETER: {
                        log_info("greeter on seat '%s' terminated, starting user session", s->name);

                        assert(s->greeter_ipc);
                        char buf[256];
                        ssize_t n = ipc_recv(s->greeter_ipc, buf, sizeof(buf) - 1);
                        ipc_close(s->greeter_ipc);
                        s->greeter_ipc = NULL;

                        const char *username, *password;
                        if (n <= 0 || parse_credentials(buf, n, &username, &password) < 0) {
                            log_error("failed to read credentials from greeter on %s", s->name);
                            goto restart_greeter;
                        }

                        sleep(1); /* SHORTCUT: avoid tight crash-loop */
                        if (session_start(username, password, PAM_CONF_PATH, s) != 0) {
                            log_error("failed to start session for %s on %s", username, s->name);
                            goto restart_greeter;
                        }

                        break;
                    }
                    case SEAT_SESSION: {
                        log_info("session on seat '%s' terminated", s->name);
                        /* falls through to restart_greeter */
                    restart_greeter:
                        log_info("restarting greeter on seat '%s'", s->name);
                        sleep(1); /* SHORTCUT: avoid tight crash-loop */
                        if (greeter_start(PAM_CONF_PATH, s) != 0) {
                            log_error("failed to restart greeter on %s", s->name);
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
