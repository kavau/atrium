#pragma once

/*
 * config.h - runtime configuration loaded from /etc/atrium.conf.
 */

/* Load configuration from CONFIG_PATH. Safe to call multiple times in order to
refresh the config. Logs a warning and falls back to defaults if the config file
is missing. */
void config_load(void);

/* Like config_load(), but reads from `path' instead of CONFIG_PATH. */
void config_load_path(const char *path);

/* Re-evaluate the stored configuration for the given seat. Applies the general
keys as well as any seat-specific overrides. Call this in the session runner
after the fork, when the seat is known. */
void config_apply_seat_overrides(const char *seat);

/* Accessors return loaded values or compiled-in defaults if the config file is
absent or a key is missing. */
const char *config_greeter(void);               /* greeter shell command */
const char *config_compositor(void);            /* compositor override */
const char *config_desktop(void);               /* desktop identifier */
const char *config_session_wrapper(void);       /* session wrapper script */
int         config_seat_discovery_delay(void);  /* ms to wait before seat discovery */
int         config_crash_restart_delay(void);   /* ms before greeter restart after crash */
int         config_crash_count_limit(void);     /* max crashes before giving up on a seat */
int         config_crash_window(void);          /* seconds over which crashes are counted */
int         config_drm_backoff(void);           /* ms to suppress DRM events after a crash */
int         config_allow_duplicate_login(void); /* whether to allow duplicate logins */
int         config_power_actions(void);         /* whether the greeter may power off/reboot */

/* Returns 1 if seat_id should be ignored, 0 otherwise. */
int config_is_seat_ignored(const char *seat_id);
