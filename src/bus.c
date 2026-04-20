#include "bus.h"
#include "config.h"
#include "event.h"
#include "log.h"
#include "seat.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
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
        seat_add(seat_id, object_path);
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

int bus_create_session(const char *seat_id, uint32_t vtnr, uid_t uid, pid_t pid,
                       char *session_id_out, size_t session_id_size,
                       char *obj_out, size_t obj_size,
                       char *runtime_out, size_t runtime_size,
                       int *fifo_fd_out)
{
    assert(g_bus);
    /* Non-seat0 seats must have vtnr == 0; seat0 must have vtnr > 0. */
    assert(strcmp(seat_id, "seat0") != 0 || vtnr > 0);
    assert(strcmp(seat_id, "seat0") == 0 || vtnr == 0);

    log_debug("creating session for seat=%s vtnr=%u uid=%u pid=%d", seat_id, vtnr, uid, (int)pid);

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *msg = NULL;
    sd_bus_message *reply = NULL;

    /* For seat0 pass the tty device path; other seats leave it empty.
     * VT numbers are 1-63 in practice; 32 bytes is ample. */
    char tty[32] = "";
    if (vtnr > 0)
        snprintf(tty, sizeof(tty), "/dev/tty%u", vtnr);

    int r = sd_bus_message_new_method_call(g_bus, &msg,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "CreateSession");
    if (r < 0) {
        log_error("CreateSession (message): %s", strerror(-r));
        goto cleanup;
    }

    /* Signature: u u s s s s s u s s b s s a(sv)
     *            uid pid service type class desktop seat vtnr tty display
     *            remote remote_user remote_host properties */
    r = sd_bus_message_append(msg, "uusssssussbss",
            (uint32_t)uid,
            (uint32_t)pid,  /* pid of the session leader (compositor process) */
            "atrium",       /* service name */
            "wayland",      /* session type */
            "user",         /* session class */
            CONFIG_DESKTOP_NAME,  /* desktop */
            seat_id,        /* seat */
            vtnr,           /* vtnr */
            tty,            /* tty device */
            "",             /* display */
            0,              /* remote = false */
            "",             /* remote_user */
            "");            /* remote_host */
    if (r < 0) {
        log_error("CreateSession (args): %s", strerror(-r));
        goto cleanup;
    }

    /* Empty properties array. */
    r = sd_bus_message_open_container(msg, 'a', "(sv)");
    if (r >= 0)
        r = sd_bus_message_close_container(msg);
    if (r < 0) {
        log_error("CreateSession (props): %s", strerror(-r));
        goto cleanup;
    }

    r = sd_bus_call(g_bus, msg, 0, &error, &reply);
    if (r < 0) {
        log_error("CreateSession: %s",
                error.message ? error.message : strerror(-r));
        goto cleanup;
    }

    /* Reply signature: soshusub */
    const char *session_id, *obj_path, *runtime_path;
    int fifo_fd;
    uint32_t ret_uid, ret_vtnr;
    const char *ret_seat;
    int existing;
    r = sd_bus_message_read(reply, "soshusub",
            &session_id, &obj_path, &runtime_path, &fifo_fd,
            &ret_uid, &ret_seat, &ret_vtnr, &existing);
    if (r < 0) {
        log_error("CreateSession (reply): %s", strerror(-r));
        goto cleanup;
    }

    log_debug("session created: id=%s object=%s", session_id, obj_path);

    snprintf(session_id_out, session_id_size, "%s", session_id);
    snprintf(obj_out, obj_size, "%s", obj_path);
    snprintf(runtime_out, runtime_size, "%s", runtime_path);

    /* dup the fifo fd with CLOEXEC so it survives sd_bus_message_unref
     * below but does not leak into child processes (e.g. udevadm settle). */
    *fifo_fd_out = fcntl(fifo_fd, F_DUPFD_CLOEXEC, 0);
    if (*fifo_fd_out < 0) {
        log_error("CreateSession: dup fifo: %s", strerror(errno));
        r = -errno;
    }

cleanup:
    sd_bus_error_free(&error);
    sd_bus_message_unref(msg);
    sd_bus_message_unref(reply);
    return (r < 0) ? -1 : 0;
}

int bus_activate_session(const char *session_object)
{
    assert(g_bus);

    log_debug("activating session %s", session_object);

    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(g_bus,
            "org.freedesktop.login1",
            session_object,
            "org.freedesktop.login1.Session",
            "Activate",
            &error, NULL, "");
    if (r < 0)
        log_error("ActivateSession: %s",
                error.message ? error.message : strerror(-r));
    sd_bus_error_free(&error);
    return (r < 0) ? -1 : 0;
}

/* -------------------------------------------------------------------------
 * Signal subscriptions
 * ------------------------------------------------------------------------- */

/* Stored callback pointers for SeatNew / SeatRemoved handlers. */
static on_seat_new_fn     g_on_seat_new     = NULL;
static on_seat_removed_fn g_on_seat_removed = NULL;

/* Per-seat context for PropertiesChanged handlers.
 * One slot per seat — bounded by MAX_SEATS. */
struct prop_slot {
    char                seat_id[64];
    on_can_graphical_fn cb;
};
static struct prop_slot g_prop_slots[MAX_SEATS];
static int              g_prop_slot_count = 0;

static int on_seat_new_signal(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error)
{
    (void)userdata; (void)ret_error;
    const char *seat_id = NULL, *object_path = NULL;
    int r = sd_bus_message_read(m, "so", &seat_id, &object_path);
    if (r < 0) {
        log_error("SeatNew: parse error: %s", strerror(-r));
        return 0;
    }
    log_info("SeatNew: seat_id=%s object_path=%s", seat_id, object_path);
    if (g_on_seat_new)
        g_on_seat_new(seat_id, object_path);
    return 0;
}

static int on_seat_removed_signal(sd_bus_message *m, void *userdata,
                                  sd_bus_error *ret_error)
{
    (void)userdata; (void)ret_error;
    const char *seat_id = NULL, *object_path = NULL;
    int r = sd_bus_message_read(m, "so", &seat_id, &object_path);
    if (r < 0) {
        log_error("SeatRemoved: parse error: %s", strerror(-r));
        return 0;
    }
    log_info("SeatRemoved: seat_id=%s object_path=%s", seat_id, object_path);
    if (g_on_seat_removed)
        g_on_seat_removed(seat_id, object_path);
    return 0;
}

static int on_properties_changed(sd_bus_message *m, void *userdata,
                                 sd_bus_error *ret_error)
{
    (void)ret_error;
    struct prop_slot *slot = userdata;

    /* Signature: sa{sv}as — interface, changed properties, invalidated. */
    const char *iface = NULL;
    int r = sd_bus_message_read(m, "s", &iface);
    if (r < 0) {
        log_error("PropertiesChanged(%s): parse iface: %s",
                  slot->seat_id, strerror(-r));
        return 0;
    }

    if (strcmp(iface, "org.freedesktop.login1.Seat") != 0)
        return 0;  /* ignore PropertiesChanged for other interfaces */

    int found = 0;

    /* Iterate the changed-properties dict looking for CanGraphical. */
    r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) {
        log_error("PropertiesChanged(%s): enter dict: %s",
                  slot->seat_id, strerror(-r));
        return 0;
    }

    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key = NULL;
        r = sd_bus_message_read(m, "s", &key);
        if (r < 0) break;

        if (strcmp(key, "CanGraphical") == 0) {
            int val = 0;
            r = sd_bus_message_read(m, "v", "b", &val);
            if (r >= 0) {
                log_info("PropertiesChanged: %s CanGraphical=%s",
                         slot->seat_id, val ? "true" : "false");
                found = 1;
                if (slot->cb)
                    slot->cb(slot->seat_id, val);
            } else {
                log_error("PropertiesChanged(%s): read CanGraphical: %s",
                          slot->seat_id, strerror(-r));
            }
        } else {
            /* Skip the variant value for unrelated keys. */
            r = sd_bus_message_skip(m, "v");
        }

        if (r < 0) break;
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);

    /* Check the invalidated-properties array.  Some logind versions may
     * place CanGraphical here (with no value) instead of in the changed
     * dict.  In that case we must query the property ourselves. */
    if (!found) {
        r = sd_bus_message_enter_container(m, 'a', "s");
        if (r >= 0) {
            const char *prop = NULL;
            while ((r = sd_bus_message_read(m, "s", &prop)) > 0) {
                if (strcmp(prop, "CanGraphical") == 0) {
                    const char *path = sd_bus_message_get_path(m);
                    int val = bus_query_can_graphical(path);
                    if (val >= 0) {
                        log_info("PropertiesChanged (invalidated): %s CanGraphical=%s",
                                 slot->seat_id, val ? "true" : "false");
                        if (slot->cb)
                            slot->cb(slot->seat_id, val);
                    }
                    break;
                }
            }
            sd_bus_message_exit_container(m);
        }
    }

    return 0;
}

int bus_query_can_graphical(const char *object_path)
{
    assert(g_bus);

    sd_bus_error error = SD_BUS_ERROR_NULL;
    int val = 0;
    int r = sd_bus_get_property_trivial(g_bus,
            "org.freedesktop.login1",
            object_path,
            "org.freedesktop.login1.Seat",
            "CanGraphical",
            &error, 'b', &val);
    sd_bus_error_free(&error);
    if (r < 0) {
        log_error("CanGraphical(%s): %s", object_path, strerror(-r));
        return -1;
    }
    return val ? 1 : 0;
}

int bus_subscribe_seat_signals(on_seat_new_fn on_new,
                               on_seat_removed_fn on_removed)
{
    assert(g_bus);
    g_on_seat_new     = on_new;
    g_on_seat_removed = on_removed;

    int r = sd_bus_add_match(g_bus, NULL,
            "type='signal'"
            ",sender='org.freedesktop.login1'"
            ",interface='org.freedesktop.login1.Manager'"
            ",member='SeatNew'",
            on_seat_new_signal, NULL);
    if (r < 0) {
        log_error("subscribe SeatNew: %s", strerror(-r));
        return -1;
    }

    r = sd_bus_add_match(g_bus, NULL,
            "type='signal'"
            ",sender='org.freedesktop.login1'"
            ",interface='org.freedesktop.login1.Manager'"
            ",member='SeatRemoved'",
            on_seat_removed_signal, NULL);
    if (r < 0) {
        log_error("subscribe SeatRemoved: %s", strerror(-r));
        return -1;
    }

    log_debug("subscribed to SeatNew/SeatRemoved signals");
    return 0;
}

int bus_subscribe_properties_changed(const char *seat_id,
                                     const char *object_path,
                                     on_can_graphical_fn on_changed)
{
    assert(g_bus);

    if (g_prop_slot_count >= MAX_SEATS) {
        log_error("bus_subscribe_properties_changed: slot limit reached");
        return -1;
    }

    struct prop_slot *slot = &g_prop_slots[g_prop_slot_count++];
    snprintf(slot->seat_id, sizeof(slot->seat_id), "%s", seat_id);
    slot->cb = on_changed;

    /* Build a match rule filtered to the specific seat object path. */
    char match[512];
    snprintf(match, sizeof(match),
             "type='signal'"
             ",sender='org.freedesktop.login1'"
             ",path='%s'"
             ",interface='org.freedesktop.DBus.Properties'"
             ",member='PropertiesChanged'",
             object_path);

    int r = sd_bus_add_match(g_bus, NULL, match, on_properties_changed, slot);
    if (r < 0) {
        log_error("subscribe PropertiesChanged(%s): %s", seat_id, strerror(-r));
        g_prop_slot_count--;
        return -1;
    }

    log_debug("subscribed to PropertiesChanged on %s (%s)", seat_id, object_path);
    return 0;
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
