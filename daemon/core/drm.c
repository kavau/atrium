#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-device.h>

#include "drm.h"
#include "lib/defs.h"
#include "lib/log.h"

int drm_seat_has_display(const char *seat_name) {
    sd_device_enumerator *e = NULL;
    int                   result = 0;

    int r = sd_device_enumerator_new(&e);
    if (r < 0) {
        log_error("drm_seat_has_display: sd_device_enumerator_new: %s", strerror(-r));
        return -1;
    }

    r = sd_device_enumerator_add_match_subsystem(e, "drm", true);
    if (r < 0) {
        log_error("drm_seat_has_display: add_match_subsystem: %s", strerror(-r));
        sd_device_enumerator_unref(e);
        return -1;
    }

    for (sd_device *dev = sd_device_enumerator_get_device_first(e); dev;
         dev = sd_device_enumerator_get_device_next(e)) {

        /* DRM connector entries have a "status" sysattr; card and renderD
        devices do not. Skip anything that is not a connected connector. */
        const char *status;
        if (sd_device_get_sysattr_value(dev, "status", &status) < 0)
            continue;
        if (strcmp(status, "connected") != 0)
            continue;

        /* Resolve the seat for this connector. Check the device itself first,
        then its parent (the card). Devices without ID_SEAT belong to seat0. */
        const char *id_seat = "seat0";
        if (sd_device_get_property_value(dev, "ID_SEAT", &id_seat) < 0) {
            sd_device *parent;
            if (sd_device_get_parent(dev, &parent) == 0)
                sd_device_get_property_value(parent, "ID_SEAT", &id_seat);
        }

        if (strcmp(id_seat, seat_name) == 0) {
            result = 1;
            break;
        }
    }

    sd_device_enumerator_unref(e);
    return result;
}

/* libudev is deprecated in favour of sd-device, but the sd_device_monitor
direct-fd API was not added until systemd 255, which excludes Debian 11/12 and
Ubuntu 22.04. The sd_event-based sd_device_monitor API available on older
versions requires a full sd_event loop, which is much more complex than the
libudev direct-fd interface used below. Revisit once Debian 12 and Ubuntu 22.04
are no longer targets. */

struct drm_monitor {
    struct udev         *udev;
    struct udev_monitor *mon;
};

int drm_monitor_init(drm_monitor **ret) {
    drm_monitor *m = calloc(1, sizeof(*m));
    if (!m) {
        log_error("drm_monitor_init: out of memory");
        return -1;
    }

    m->udev = udev_new();
    if (!m->udev) {
        log_error("drm_monitor_init: udev_new failed");
        goto fail;
    }

    m->mon = udev_monitor_new_from_netlink(m->udev, "udev");
    if (!m->mon) {
        log_error("drm_monitor_init: udev_monitor_new_from_netlink failed");
        goto fail;
    }

    int r = udev_monitor_filter_add_match_subsystem_devtype(m->mon, "drm", NULL);
    if (r < 0) {
        log_error("drm_monitor_init: filter_add_match_subsystem_devtype: %s", strerror(-r));
        goto fail;
    }

    r = udev_monitor_enable_receiving(m->mon);
    if (r < 0) {
        log_error("drm_monitor_init: udev_monitor_enable_receiving: %s", strerror(-r));
        goto fail;
    }

    *ret = m;
    return 0;

fail:
    if (m->mon)
        udev_monitor_unref(m->mon);
    if (m->udev)
        udev_unref(m->udev);
    free(m);
    return -1;
}

int drm_monitor_fd(drm_monitor *m) { return udev_monitor_get_fd(m->mon); }

int drm_monitor_read_seat(drm_monitor *m, char *seat_out, size_t seat_len) {
    struct udev_device *dev = udev_monitor_receive_device(m->mon);
    if (!dev) {
        log_error("drm_monitor_read_seat: udev_monitor_receive_device failed");
        return -1;
    }

    const char *sysname = udev_device_get_sysname(dev);
    const char *action = udev_device_get_action(dev);

    /* Only act on change events (display plugged in or out). */
    if (!action || strcmp(action, "change") != 0) {
        log_debug("drm monitor: %s: ignoring non-change event", sysname ? sysname : "(unknown)");
        udev_device_unref(dev);
        return 0;
    }

    /* Resolve seat: check the device then its parent, defaulting to seat0.
       udev_device_get_parent() returns a borrowed reference; no unref needed. */
    const char *id_seat = udev_device_get_property_value(dev, "ID_SEAT");
    if (!id_seat) {
        struct udev_device *parent = udev_device_get_parent(dev);
        if (parent)
            id_seat = udev_device_get_property_value(parent, "ID_SEAT");
    }
    if (!id_seat)
        id_seat = "seat0";

    log_debug("drm monitor: %s: change event for seat '%s'", sysname ? sysname : "(unknown)",
              id_seat);

    strncpy(seat_out, id_seat, seat_len - 1);
    seat_out[seat_len - 1] = '\0';
    udev_device_unref(dev);
    return 1;
}

void drm_monitor_close(drm_monitor *m) {
    udev_monitor_unref(m->mon);
    udev_unref(m->udev);
    free(m);
}
