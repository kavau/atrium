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

/* Per-seat user mapping.  Seats not listed here are skipped (no session
 * is started).  The table is terminated by a { NULL, NULL } sentinel.
 * Replaced by greeter IPC + PAM authentication in Phases 8-9. */
struct seat_user_config {
    const char *seat;
    const char *username;
};

#define CONFIG_SEAT_USERS { \
    { "seat0", "testuser" },    \
    { "seat1", "testuser" }, \
    { NULL, NULL }           \
}
