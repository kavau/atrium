#pragma once

/*
 * greeter/config.h — runtime configuration for atrium-greeter.
 *
 * Loaded from /etc/atrium-greeter.conf (or ATRIUM_SYSCONFDIR/atrium-greeter.conf).
 * Call greeter_config_load() once at startup; the accessors return compiled-in
 * defaults if the file is absent or a key is missing.
 */

void        greeter_config_load(const char *path);

int         greeter_config_blank_timeout(void);   /* idle blanking timeout (s); 0 = disabled */
const char *greeter_config_cursor_theme(void);    /* GTK cursor theme name                   */
int         greeter_config_cursor_size(void);     /* GTK cursor size                         */
int         greeter_config_base_font_size(void);  /* base font size in px                    */
