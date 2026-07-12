/*
 * defs.h - compile-time constants
 */

/* Path to daemon configuration file. */
#define CONFIG_PATH "/etc/atrium.conf"

/* Maximum length for seat names. */
#define MAX_LEN_SEAT 64

/* Max size of messages (e.g. credentials) passed between greeter and daemon.
Must stay below PIPE_BUF (4096 on Linux) to guarantee atomic writes. */
#define MAX_LEN_IPC_MSG 512

/* PAM configuration directory. */
#define PAM_CONF_PATH "/etc/pam.d"

/* Headless mode: skip VT allocation/activation and connected display check. For
testing from an existing graphical session with a fake greeter/compositor. */
#define HEADLESS 0

/* User account used to run the greeter process. */
#define GREETER_USERNAME "atriumdm"
