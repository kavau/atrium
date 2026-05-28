/*
 * defs.h - compile-time configuration
 */

#define MAX_LEN_SEAT 64

/* SHORTCUT: pass the PAM config path in the environment instead */
#define PAM_CONF_PATH "/etc/pam.d"

/* SHORTCUT: the following parameters should be read from a runtime config file. */

/* Seat names to ignore during enumeration (e.g. monitor-less seats). Set to
{NULL} to disable. */
#define IGNORE_SEATS {NULL}

/* Number of seconds to wait at startup before seat discovery. */
#define SEAT_DISCOVERY_DELAY 2

/* Headless mode - set to 1 to skip VT allocation/activation (for testing with a
fake greeter/compositor from a graphical session)*/
#define HEADLESS 0

/* Greeter to launch on each seat. ATRIUM_GREETER_DIR is set by meson to
the distro-appropriate libexecdir. */
#define GREETER                                               \
    "/usr/bin/cage -s -- /usr/bin/foot -f monospace:size=18 " \
    "-o colors-dark.background=000000 "                       \
    ATRIUM_GREETER_DIR "/atrium-txt-greeter"

/* User to launch greeter as. */
#define GREETER_USERNAME "atriumdm"

/* Compositor to launch for user sessions on each seat. */
#define COMPOSITOR                                           \
    "/usr/bin/cage -s -m last -- foot -f monospace:size=18 " \
    "-o colors-dark.background=000000"

/* Desktop environment name exported as XDG_CURRENT_DESKTOP / XDG_SESSION_DESKTOP. */
#define DESKTOP_NAME "cage"
