/*
 * drm-test.c - a tool for testing drm status and hotplug events.
 *
 * For each seat, outputs whether a display is connected. Also outputs each
 * connector-change event and re-prints the per-seat status.
 *
 * Usage: ./build/atrium-drm-test 2>/dev/null
 */

#define LOG_PREFIX "drm-test"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

#include "daemon/core/bus.h"
#include "daemon/core/drm.h"
#include "daemon/core/seat.h"
#include "lib/log.h"

static void print_seat_displays(void) {
    for (seat *s = seat_first(); s; s = seat_next(s)) {
        int         r = drm_seat_has_display(s->name);
        const char *status = r > 0 ? "connected" : r == 0 ? "no display" : "error";
        printf("  %-12s %s\n", s->name, status);
    }
}

static void on_seat_discovered(const char *seat_id, void *userdata) {
    (void)userdata;
    if (!seat_add(seat_id, 0))
        log_error("seat_add failed for '%s'", seat_id);
}

int main(void) {
    if (bus_open() < 0)
        return EXIT_FAILURE;
    bus_enumerate_seats(on_seat_discovered, NULL);
    bus_close();

    if (!seat_first()) {
        log_error("no seats found");
        return EXIT_FAILURE;
    }

    printf("DRM display status at startup:\n");
    print_seat_displays();

    drm_monitor *mon;
    if (drm_monitor_init(&mon) < 0) {
        log_error("drm_monitor_init failed");
        return EXIT_FAILURE;
    }

    printf("\nMonitoring for DRM events (Ctrl-C to quit)...\n");
    fflush(stdout);

    struct pollfd pfd = {.fd = drm_monitor_fd(mon), .events = POLLIN};

    while (1) {
        int r = poll(&pfd, 1, -1);
        if (r < 0) {
            log_syserr("poll");
            break;
        }
        if (!(pfd.revents & POLLIN))
            continue;

        char seat_name[64];
        r = drm_monitor_read_seat(mon, seat_name, sizeof(seat_name));
        if (r < 0) {
            log_error("drm_monitor_read_seat failed");
            break;
        }
        if (r == 0)
            continue; /* non-change event, ignored */

        printf("\nConnector change on seat '%s'\n", seat_name);
        printf("DRM display status:\n");
        print_seat_displays();
        fflush(stdout);
    }

    drm_monitor_close(mon);
    return EXIT_FAILURE;
}
