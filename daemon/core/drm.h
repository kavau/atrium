#pragma once

/*
 * drm.h - DRM monitoring
 */

/* Returns 1 if seat_name has at least one connected DRM display, 0 if not, or
-1 on error. */
int drm_seat_has_display(const char *seat_name);
