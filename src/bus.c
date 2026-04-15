#include "bus.h"
#include "event.h"
#include "log.h"
#include "seat.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <systemd/sd-bus.h>

static sd_bus *g_bus = NULL;

/* Event loop callback for the sd-bus fd. Drains all pending messages by
 * calling sd_bus_process() until it returns 0 (nothing left to dispatch).
 * Each call either handles the message internally (e.g. protocol bookkeeping)
 * or dispatches it to a registered handler. */
static void on_bus(int fd, void *userdata)
{
    (void)fd;
    (void)userdata;

    int r, count = 0;
    do {
        r = sd_bus_process(g_bus, NULL);
        if (r > 0) count++;
    } while (r > 0);

    if (count > 0)
        log_debug("processed %d bus messages", count);

    if (r < 0)
        log_error("sd_bus_process: %s", strerror(-r));
}

int bus_enumerate_seats(void)
{
    assert(g_bus);
    log_debug("enumerating seats via ListSeats");

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(g_bus,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "ListSeats",
            &error,
            &reply,
            "");
    if (r < 0) {
        log_error("ListSeats: %s", error.message);
        goto cleanup;
    }

    /* Reply type is a(so): array of (seat-id, object-path) structs. */
    r = sd_bus_message_enter_container(reply, 'a', "(so)");
    if (r < 0) {
        log_error("ListSeats reply: %s", strerror(-r));
        goto cleanup;
    }

    while ((r = sd_bus_message_enter_container(reply, 'r', "so")) > 0) {
        const char *seat_id = NULL, *object_path = NULL;
        r = sd_bus_message_read(reply, "so", &seat_id, &object_path);
        if (r < 0) {
            log_error("ListSeats read: %s", strerror(-r));
            break;
        }
        seat_add(seat_id);
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_exit_container(reply);

    if (r >= 0)
        log_debug("enumeration complete, found %d seats", seat_count());

cleanup:
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return (r < 0) ? -1 : 0;
}

int bus_open(void)
{
    int r = sd_bus_open_system(&g_bus);
    if (r < 0) {
        log_error("sd_bus_open_system: %s", strerror(-r));
        return -1;
    }

    /* Wire the sd-bus fd into the event loop for future async messages. */
    r = event_add(sd_bus_get_fd(g_bus), on_bus, NULL);
    if (r < 0) {
        sd_bus_unref(g_bus);
        g_bus = NULL;
        return -1;
    }

    log_debug("system bus opened, fd=%d", sd_bus_get_fd(g_bus));
    return 0;
}

void bus_close(void)
{
    if (!g_bus)
        return;
    log_debug("closing system bus");
    event_remove(sd_bus_get_fd(g_bus));
    sd_bus_unref(g_bus);
    g_bus = NULL;
}
