/*
 * defs.h - compile-time constants
 */

#define MAX_LEN_SEAT 64

/* PAM configuration directory. */
#define PAM_CONF_PATH "/etc/pam.d"

/* Headless mode: skip VT allocation/activation - for testing from an existing
 * graphical session with a fake greeter/compositor. */
#define HEADLESS 0

/* User account used to run the greeter process. */
#define GREETER_USERNAME "atriumdm"
