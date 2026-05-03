/*
 * defs.h - compile-time configuration
 */

#define MAX_LEN_SEAT 64

/* SHORTCUT: hardcoded seats and user credentials */
#define SEAT_CONFIGS {"seat0", "alice", "password123"}, {"seat1", "bob", "password456"}

/* SHORTCUT: pass the PAM config path in the environment instead */
#define PAM_CONF_PATH "data/pam"

/* SHORTCUT: the following parameters should be read from a runtime config file. */

/* Compositor to launch on each seat. */
#define COMPOSITOR \
    "/usr/bin/cage -s -m last -- foot -f monospace:size=18 -o colors-dark.background=000000"
