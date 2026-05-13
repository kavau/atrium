#include "bus.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include "lib/log.h"

static sd_bus *g_bus = NULL;

int bus_open(void) {
    int r = sd_bus_open_system(&g_bus);
    if (r < 0) {
        log_error("bus_open: sd_bus_open_system: %s", strerror(-r));
        return -1;
    }
    log_debug("bus_open: system bus opened");
    return 0;
}

void bus_close(void) {
    if (!g_bus)
        return;
    log_debug("bus_close: closing system bus");
    sd_bus_unref(g_bus);
    g_bus = NULL;
}

int bus_create_session(const char *seat_id, uint32_t vtnr, uid_t uid, pid_t pid,
                       const char *desktop, const char *session_class, char *session_id_out,
                       size_t session_id_size, char *obj_out, size_t obj_size, char *runtime_out,
                       size_t runtime_size, int *fifo_fd_out) {
    assert(g_bus);
    /* Non-seat0 seats must have vtnr == 0; seat0 must have vtnr > 0. */
    assert(strcmp(seat_id, "seat0") != 0 || vtnr > 0);
    assert(strcmp(seat_id, "seat0") == 0 || vtnr == 0);

    log_debug("bus_create_session: seat=%s vtnr=%u uid=%u pid=%d class=%s", seat_id, vtnr,
              (unsigned)uid, (int)pid, session_class);

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *msg = NULL;
    sd_bus_message *reply = NULL;

    /* For seat0 pass the tty device path; other seats leave it empty. */
    char tty[32] = "";
    if (vtnr > 0)
        snprintf(tty, sizeof(tty), "/dev/tty%u", vtnr);

    int r = sd_bus_message_new_method_call(g_bus, &msg, "org.freedesktop.login1",
                                           "/org/freedesktop/login1",
                                           "org.freedesktop.login1.Manager", "CreateSession");
    if (r < 0) {
        log_error("bus_create_session: new_method_call: %s", strerror(-r));
        goto cleanup;
    }

    r = sd_bus_message_append(msg, "uusssssussbss", (uint32_t)uid, (uint32_t)pid,
                              "atrium",               /* service name */
                              "wayland",              /* session type */
                              session_class,          /* "greeter" or "user" */
                              desktop,                /* desktop name */
                              seat_id, vtnr, tty, "", /* display */
                              0,                      /* remote = false */
                              "",                     /* remote_user */
                              "");                    /* remote_host */
    if (r < 0) {
        log_error("bus_create_session: append args: %s", strerror(-r));
        goto cleanup;
    }

    /* Empty properties array. */
    r = sd_bus_message_open_container(msg, 'a', "(sv)");
    if (r >= 0)
        r = sd_bus_message_close_container(msg);
    if (r < 0) {
        log_error("bus_create_session: properties container: %s", strerror(-r));
        goto cleanup;
    }

    r = sd_bus_call(g_bus, msg, 0, &error, &reply);
    if (r < 0) {
        log_error("bus_create_session: CreateSession: %s",
                  error.message ? error.message : strerror(-r));
        goto cleanup;
    }

    const char *session_id, *obj_path, *runtime_path;
    int fifo_fd;
    uint32_t ret_uid, ret_vtnr;
    const char *ret_seat;
    int existing;
    r = sd_bus_message_read(reply, "soshusub", &session_id, &obj_path, &runtime_path, &fifo_fd,
                            &ret_uid, &ret_seat, &ret_vtnr, &existing);
    if (r < 0) {
        log_error("bus_create_session: read reply: %s", strerror(-r));
        goto cleanup;
    }

    log_debug("bus_create_session: session created: id=%s runtime=%s", session_id, runtime_path);

    snprintf(session_id_out, session_id_size, "%s", session_id);
    snprintf(obj_out, obj_size, "%s", obj_path);
    snprintf(runtime_out, runtime_size, "%s", runtime_path);

    /* dup the fifo fd with O_CLOEXEC so it survives sd_bus_message_unref
     * but does not leak into child processes. */
    *fifo_fd_out = fcntl(fifo_fd, F_DUPFD_CLOEXEC, 0);
    if (*fifo_fd_out < 0) {
        log_error("bus_create_session: dup fifo: %s", strerror(errno));
        r = -errno;
    }

cleanup:
    sd_bus_error_free(&error);
    sd_bus_message_unref(msg);
    sd_bus_message_unref(reply);
    return (r < 0) ? -1 : 0;
}

int bus_activate_session(const char *session_object) {
    assert(g_bus);
    log_debug("bus_activate_session: %s", session_object);

    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(g_bus, "org.freedesktop.login1", session_object,
                               "org.freedesktop.login1.Session", "Activate", &error, NULL, "");
    if (r < 0)
        log_error("bus_activate_session: %s", error.message ? error.message : strerror(-r));
    sd_bus_error_free(&error);
    return (r < 0) ? -1 : 0;
}
