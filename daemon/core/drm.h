#pragma once

/*
 * drm.h - DRM status and event monitoring
 */

#include <stddef.h>

/* Returns 1 if seat_name has at least one connected DRM display, 0 if not, or
-1 on error. */
int drm_seat_has_display(const char *seat_name);

/* DRM event monitor handle. */
typedef struct drm_monitor drm_monitor;

/* Initialize DRM subsystem event monitor. Returns 0 on success, -1 on error. */
int drm_monitor_init(drm_monitor **ret);

/* Return the monitoring fd to add to poll(). */
int drm_monitor_fd(drm_monitor *m);

/* Read one pending event. If it is a connector-change event, writes the seat
name into seat_out and returns 1. Otherwise returns 0 if the event should be
ignored, or -1 on error. */
int drm_monitor_read_seat(drm_monitor *m, char *seat_out, size_t seat_len);

/* Stop the monitor and free its resources. */
void drm_monitor_close(drm_monitor *m);
