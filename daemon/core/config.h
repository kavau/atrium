#pragma once

/*
 * config.h - runtime configuration loaded from /etc/atrium.conf.
 */

/* Load configuration from CONFIG_PATH. Safe to call multiple times in order to
refresh the config. Logs a warning and falls back to defaults if the config file
is missing. */
void config_load(void);

/* Accessors return loaded values or compiled-in defaults if the config file is
absent or a key is missing. */
const char *config_greeter(void);               /* greeter shell command */
const char *config_compositor(void);            /* compositor override */
const char *config_desktop(void);               /* desktop identifier */
int         config_seat_discovery_delay(void);  /* ms to wait before seat discovery */
int         config_crash_restart_delay(void);   /* ms before greeter restart after crash */
int         config_crash_count_limit(void);     /* max crashes before giving up on a seat */
int         config_crash_window(void);          /* seconds over which crashes are counted */
int         config_allow_duplicate_login(void); /* whether to allow duplicate logins */

/* Returns 1 if seat_id should be ignored, 0 otherwise. */
int config_is_seat_ignored(const char *seat_id);
