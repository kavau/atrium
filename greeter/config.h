#pragma once

/*
 * config.h - greeter runtime configuration loaded from /etc/atrium-greeter.conf.
 */

/* Load configuration from path. Logs a warning and falls back to defaults if
the file is missing. */
void greeter_config_load(const char *path);

/* Accessors return values from the config file, or compiled-in defaults if it
is absent or a key is missing. */
int         greeter_config_blank_timeout(void);  /* seconds; 0 = disabled */
int         greeter_config_base_font_size(void); /* px */
const char *greeter_config_login_label(void);
const char *greeter_config_cursor_theme(void);
int         greeter_config_cursor_size(void);    /* px */
const char *greeter_config_theme(void);
