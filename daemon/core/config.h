#pragma once

/*
 * config.h - runtime configuration loaded from /etc/atrium.conf.
 */

/* Load configuration from path. Logs a warning and falls back to defaults if
the file is missing. */
void config_load(const char *path);

/* Accessors return loaded values or compiled-in defaults if the config file is
absent or a key is missing. */
const char *config_greeter(void);      /* greeter shell command */
const char *config_compositor(void);   /* compositor override */
const char *config_desktop(void);      /* desktop identifier */
int config_seat_discovery_delay(void); /* ms to wait before seat discovery */
int config_crash_restart_delay(void);  /* ms before greeter restart after crash */

/* Returns 1 if seat_id should be ignored, 0 otherwise. */
int config_is_seat_ignored(const char *seat_id);
