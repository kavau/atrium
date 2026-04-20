#pragma once

/*
 * drm.h — udev DRM monitor
 *
 * Watches the drm subsystem for hotplug events (connector status changes,
 * GPU add/remove).  In Phase 11 Step 2 the monitor only logs events; later
 * steps will act on them to start/stop greeters.
 */

/* Open a udev_monitor on the drm subsystem and register its fd with the
 * event loop.  Returns 0 on success, -1 on error (error is logged). */
int drm_init(void);

/* Deregister the drm fd from the event loop and release udev resources. */
void drm_close(void);

/* Return 1 if seat_id has at least one connected DRM connector, 0 if not,
 * -1 on error.  Reads connector status files under /sys/class/drm/ directly;
 * can be called at any time after drm_init(). */
int drm_seat_has_display(const char *seat_id);
