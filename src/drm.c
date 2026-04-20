#include "drm.h"
#include "event.h"
#include "log.h"

#include <libudev.h>
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

        log_info("drm event: action=%s devpath=%s ID_SEAT=%s",
                 action  ? action  : "(null)",
                 devpath ? devpath : "(null)",
                 seat_id);

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
