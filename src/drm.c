#include "drm.h"
#include "event.h"
#include "log.h"

#include <assert.h>
#include <glob.h>
#include <libudev.h>
#include <stdio.h>
#include <string.h>

static struct udev         *g_udev    = NULL;
static struct udev_monitor *g_monitor = NULL;

/*
 * on_drm_event — called by the event loop when the udev monitor fd is
 * readable.  Drains all pending events; each is logged with its action,
 * devpath, and ID_SEAT property.
 *
 * Expected events on the drm subsystem:
 *   change  — connector status changed (cable plugged/unplugged)
 *   add     — DRM device added (GPU hot-plugged)
 *   remove  — DRM device removed (GPU hot-unplugged)
 *
 * Step 4 will act on these events.
 */
static void on_drm_event(int fd, void *userdata)
{
    (void)fd;
    (void)userdata;

    struct udev_device *dev;
    while ((dev = udev_monitor_receive_device(g_monitor)) != NULL) {
        const char *action  = udev_device_get_action(dev);
        const char *devpath = udev_device_get_devpath(dev);
        const char *seat_id = udev_device_get_property_value(dev, "ID_SEAT");

        /* Devices without an ID_SEAT property belong to seat0 by default. */
        if (!seat_id)
            seat_id = "seat0";

        int has_display = drm_seat_has_display(seat_id);
        log_info("drm event: action=%s devpath=%s ID_SEAT=%s has_display=%s",
                 action  ? action  : "(null)",
                 devpath ? devpath : "(null)",
                 seat_id,
                 has_display > 0 ? "true" : (has_display == 0 ? "false" : "error"));

        udev_device_unref(dev);
    }
}

int drm_init(void)
{
    g_udev = udev_new();
    if (!g_udev) {
        log_error("udev_new failed");
        return -1;
    }

    g_monitor = udev_monitor_new_from_netlink(g_udev, "udev");
    if (!g_monitor) {
        log_error("udev_monitor_new_from_netlink failed");
        udev_unref(g_udev);
        g_udev = NULL;
        return -1;
    }

    int r = udev_monitor_filter_add_match_subsystem_devtype(g_monitor, "drm", NULL);
    if (r < 0) {
        log_error("udev_monitor_filter_add_match_subsystem_devtype: %s", strerror(-r));
        goto fail;
    }

    r = udev_monitor_enable_receiving(g_monitor);
    if (r < 0) {
        log_error("udev_monitor_enable_receiving: %s", strerror(-r));
        goto fail;
    }

    int monitor_fd = udev_monitor_get_fd(g_monitor);
    r = event_add(monitor_fd, on_drm_event, NULL);
    if (r < 0)
        goto fail;

    log_debug("drm monitor started, fd=%d", monitor_fd);
    return 0;

fail:
    udev_monitor_unref(g_monitor);
    g_monitor = NULL;
    udev_unref(g_udev);
    g_udev = NULL;
    return -1;
}

int drm_seat_has_display(const char *seat_id)
{
    assert(g_udev);

    struct udev_enumerate *e = udev_enumerate_new(g_udev);
    if (!e) {
        log_error("drm_seat_has_display: udev_enumerate_new failed");
        return -1;
    }

    udev_enumerate_add_match_subsystem(e, "drm");
    udev_enumerate_scan_devices(e);

    /*
     * We cannot use udev_enumerate_add_match_property(e, "ID_SEAT", seat_id)
     * here because that filter queries the udev database, which only stores
     * ID_SEAT for devices explicitly assigned via `loginctl attach`.  Devices
     * that belong to seat0 by default (never explicitly assigned) have no
     * ID_SEAT entry in the database and would be silently excluded.
     *
     * Instead we enumerate all drm cards and read the live property from each
     * device object, treating absent ID_SEAT as seat0.
     */
    int found = 0;
    struct udev_list_entry *entry;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
        const char *syspath = udev_list_entry_get_name(entry);

        struct udev_device *dev = udev_device_new_from_syspath(g_udev, syspath);
        if (!dev)
            continue;

        /* Devices with no ID_SEAT property implicitly belong to seat0. */
        const char *dev_seat = udev_device_get_property_value(dev, "ID_SEAT");
        if (!dev_seat)
            dev_seat = "seat0";

        int seat_match = (strcmp(dev_seat, seat_id) == 0);
        udev_device_unref(dev);

        if (!seat_match)
            continue;

        /* Glob for connector status files under this card's sysfs dir. */
        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s/card*-*/status", syspath);

        glob_t gl;
        if (glob(pattern, GLOB_NOSORT, NULL, &gl) != 0) {
            globfree(&gl);
            continue;
        }

        for (size_t i = 0; i < gl.gl_pathc && !found; i++) {
            FILE *f = fopen(gl.gl_pathv[i], "r");
            if (!f)
                continue;
            char status[32] = {0};
            if (fgets(status, sizeof(status), f)) {
                /* Strip trailing newline. */
                size_t len = strlen(status);
                if (len > 0 && status[len - 1] == '\n')
                    status[len - 1] = '\0';
                log_debug("drm_seat_has_display: %s -> %s", gl.gl_pathv[i], status);
                if (strcmp(status, "connected") == 0)
                    found = 1;
            }
            fclose(f);
        }
        globfree(&gl);
        if (found)
            break;
    }

    udev_enumerate_unref(e);
    return found;
}

void drm_close(void)
{
    if (!g_monitor)
        return;
    log_debug("closing drm monitor");
    event_remove(udev_monitor_get_fd(g_monitor));
    udev_monitor_unref(g_monitor);
    g_monitor = NULL;
    udev_unref(g_udev);
    g_udev = NULL;
}
