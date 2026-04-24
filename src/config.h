#pragma once

/*
 * config.h — compile-time configuration
 *
 * SHORTCUT: Several values here are hardcoded and will eventually be
 * determined dynamically (compositor and desktop name from a session .desktop
 * file; delays replaced by event-driven alternatives).
 */

/* Compositor to launch on each seat. */
#define CONFIG_COMPOSITOR       "sway"

/* Desktop identifier passed to logind's CreateSession. */
#define CONFIG_DESKTOP_NAME     "sway"

/* Seconds to wait before enumerating seats at startup.
 * Gives logind time to finish processing udev seat events on early boot.
 * SHORTCUT: replaced by SeatNew/SeatRemoved signal monitoring once Phase 11
 * (hotplug) is reimplemented. */
#define CONFIG_SEAT_ENUM_DELAY  5

/* Seconds to wait before restarting a crashed compositor.
 * SHORTCUT: replaced by timerfd-based crash-loop detection in Phase 12. */
#define CONFIG_RESTART_DELAY    2

/* SHORTCUT: Seconds of idle time before blanking the greeter display.
 * After this timeout a black fullscreen overlay is shown to prevent screen
 * burn-in.  The overlay is hidden on any input event.  This does NOT power
 * down the display (LCD backlight stays on) — true DPMS blanking via DRM
 * would require daemon-side DRM control.  Set to 0 to disable blanking. */
#define CONFIG_BLANK_TIMEOUT    300

/* Array of seat names to ignore during enumeration (e.g. monitorless seats
 * that would crash-loop the greeter).  Set to { NULL } to disable. */
#define CONFIG_IGNORE_SEATS     { NULL }

/* Credential protocol limits.
 * MAX_USERNAME_LEN matches POSIX LOGIN_NAME_MAX (255 bytes including NUL).
 * MAX_PASSWORD_LEN follows common PAM convention (512 bytes including NUL).
 * These limits are enforced in both the greeter and daemon to prevent
 * buffer overruns and ensure atomic pipe writes under PIPE_BUF (4096). */
#define CONFIG_MAX_USERNAME_LEN  255
#define CONFIG_MAX_PASSWORD_LEN  512

/* SHORTCUT: Array of usernames that can log in without a password.
 * For these users the greeter skips the password screen and sends empty
 * credentials; the daemon skips PAM authentication and resolves uid/gid
 * directly via getpwnam().  No PAM session is opened (no loginuid, no
 * pam_limits, no keyring) — this is a development convenience, not a
 * production feature.  Set to { NULL } to require passwords for all users. */
#define CONFIG_PASSWORDLESS_USERS  { NULL }

/* Greeter command — cage kiosk compositor hosting atrium-greeter.
 * GREETER_USER is a dedicated system account (uid < 1000, no home directory).
 *
 * To change the username from the default "atriumdm", update both:
 *   - CONFIG_GREETER_USER below
 *   - GREETER_USER in tools/create-greeter-user.sh
 *
 * Note: all greeter instances share the same XDG_RUNTIME_DIR
 * (/run/user/<uid>), so cage instances on different seats compete for
 * wayland-0.lock.  The loser falls back to wayland-1, wayland-2, etc.
 * This is harmless — cage handles the fallback gracefully. */
#define CONFIG_GREETER_CMD  "/usr/bin/cage"
#define CONFIG_GREETER_ARGS { "/usr/bin/cage", "-s", \
                              "/usr/local/libexec/atrium-greeter", NULL }
#define CONFIG_GREETER_USER "atriumdm"

/* Cursor theme and size used by the greeter.
 * GTK4 under cage does not inherit cursor settings from the user environment,
 * so both must be set explicitly via GtkSettings.  Adwaita is available on
 * all systems that have GTK4 installed (adwaita-icon-theme is a dependency).
 * The size value is passed to gtk-cursor-theme-size; due to how cage scales
 * cursor sprites, larger values produce a visually smaller cursor — 32 is a
 * normal desktop size.  Adjust if the cursor looks too large or too small. */
#define CONFIG_CURSOR_THEME "Adwaita"
#define CONFIG_CURSOR_SIZE  36

/* Base font size (in px) for the greeter UI.  All text sizes are derived from
 * this value: heading = base * 3/2, user buttons = base * 6/5, entry text
 * and labels = base.  Increase for HiDPI displays. */
#define CONFIG_BASE_FONT_SIZE 20
