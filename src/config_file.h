#pragma once

/*
 * config_file.h — runtime configuration loaded from /etc/atrium.conf.
 *
 * Call config_load() once at daemon startup.  The accessors return the
 * loaded values, or compiled-in defaults if the file is absent or a key
 * is missing.
 */

/* Load configuration from path.  Missing file or missing individual keys
 * fall back to compiled-in defaults — partial configs are valid.  An
 * unreadable file logs a warning and falls back to defaults entirely.
 * Unknown keys are warned and skipped. */
void config_load(const char *path);

const char  *config_greeter(void);        /* greeter shell command          */
const char  *config_compositor(void);     /* compositor shell command       */
const char  *config_desktop(void);        /* desktop identifier for logind  */
int          config_restart_delay(void);        /* seconds before greeter restart    */
int          config_seat_enum_delay(void);      /* startup seat enumeration delay    */
const char **config_ignore_seats(void);         /* NULL-terminated seat name list    */
const char **config_passwordless_users(void);   /* NULL-terminated username list     */
