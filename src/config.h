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
#define CONFIG_RESTART_DELAY    5

/* Greeter command — cage kiosk compositor hosting atrium-greeter.
 *
 * SHORTCUT: GREETER_UID/GID should be a dedicated system account
 * (e.g. 'atrium').  Using uid 1000 (primary user) for now. */
#define CONFIG_GREETER_CMD  "/usr/bin/cage"
#define CONFIG_GREETER_ARGS { "/usr/bin/cage", "-s", \
                              "/usr/local/libexec/atrium-greeter", NULL }
#define CONFIG_GREETER_UID  1000
#define CONFIG_GREETER_GID  1000
