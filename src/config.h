#pragma once

/*
 * config.h — compile-time configuration
 *
 * SHORTCUT: All values here are hardcoded and will eventually be determined
 * dynamically (compositor by greeter/desktop file, username by PAM
 * authentication, desktop name from session .desktop, delays removed).
 */

/* Compositor to launch on each seat. */
#define CONFIG_COMPOSITOR       "sway"

/* Desktop identifier passed to logind's CreateSession. */
#define CONFIG_DESKTOP_NAME     "atrium-dev"

/* Seconds to wait before enumerating seats at startup.
 * Gives logind time to finish processing udev seat events on early boot.
 * Replaced by SeatNew/SeatRemoved signal monitoring in Phase 6. */
#define CONFIG_SEAT_ENUM_DELAY  2

/* Seconds to wait before restarting a crashed compositor.
 * Replaced by timerfd-based crash-loop detection in Phase 7. */
#define CONFIG_RESTART_DELAY    2

/* SHORTCUT: Seconds of idle time before blanking the greeter display.
 * After this timeout a black fullscreen overlay is shown to prevent screen
 * burn-in.  The overlay is hidden on any input event.  This does NOT power
 * down the display (LCD backlight stays on) — true DPMS blanking via DRM
 * would require daemon-side DRM control.  Set to 0 to disable blanking. */
#define CONFIG_BLANK_TIMEOUT    300

/* Seat name to ignore during enumeration (e.g. a monitorless seat that
 * crashes the greeter).  Set to NULL or "" to disable.
 * To ignore multiple seats, extend this to a NULL-terminated array. */
#define CONFIG_IGNORE_SEATS     ""

/* SHORTCUT: Users that can log in without a password.
 * For these users the greeter skips the password screen and sends empty
 * credentials; the daemon skips PAM authentication and resolves uid/gid
 * directly via getpwnam().  No PAM session is opened (no loginuid, no
 * pam_limits, no keyring) — this is a development convenience, not a
 * production feature.  Set to { NULL } to require passwords for all users. */
#define CONFIG_PASSWORDLESS_USERS  { NULL }

/* Greeter command — cage kiosk compositor hosting atrium-greeter.
 *
 * SHORTCUT: GREETER_UID/GID should be a dedicated system account
 * (e.g. 'atrium').  Using uid 1000 (primary user) for now. */
#define CONFIG_GREETER_CMD  "/usr/bin/cage"
#define CONFIG_GREETER_ARGS { "/usr/bin/cage", "-s", \
                              "/usr/local/libexec/atrium-greeter", NULL }
#define CONFIG_GREETER_UID  1000
#define CONFIG_GREETER_GID  1000
