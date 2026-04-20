#pragma once

/*
 * drm.h — udev DRM monitor
 *
 * Watches the drm subsystem for hotplug events (connector status changes,
 * GPU add/remove).  Fires a callback on each event so the caller can
 * start or stop greeters in response.
 */

/* Callback invoked for each DRM udev event.
 *   seat_id — seat the device belongs to (never NULL; implicit seat0 resolved)
 *   action  — "change" (connector plug/unplug), "add" (GPU added),
 *             "remove" (GPU removed)
 */
typedef void (*on_drm_event_fn)(const char *seat_id, const char *action);

/* Open a udev_monitor on the drm subsystem and register its fd with the
 * event loop.  callback is invoked for each event (may be NULL for
 * log-only mode).  Returns 0 on success, -1 on error (error is logged). */
int drm_init(on_drm_event_fn callback);

/* Deregister the drm fd from the event loop and release udev resources. */
void drm_close(void);

/* Return 1 if seat_id has at least one connected DRM connector, 0 if not,
 * -1 on error.  Reads connector status files under /sys/class/drm/ directly;
 * can be called at any time after drm_init(). */
int drm_seat_has_display(const char *seat_id);
