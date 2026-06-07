#include "config.h"

#include <string.h>

#include "ini.h"
#include "lib/conf_helpers.h"
#include "lib/log.h"

#define DEFAULT_BLANK_TIMEOUT  300
#define DEFAULT_BASE_FONT_SIZE  28
#define DEFAULT_LOGIN_LABEL    "Log in"
#define DEFAULT_CURSOR_THEME   "Adwaita"
#define DEFAULT_CURSOR_SIZE     36

static struct {
    int  blank_timeout;
    int  base_font_size;
    char login_label[128];
    char cursor_theme[128];
    int  cursor_size;
    char theme[512];
    char background_image[512];
} g_cfg = {
    .blank_timeout    = DEFAULT_BLANK_TIMEOUT,
    .base_font_size   = DEFAULT_BASE_FONT_SIZE,
    .login_label      = DEFAULT_LOGIN_LABEL,
    .cursor_theme     = DEFAULT_CURSOR_THEME,
    .cursor_size      = DEFAULT_CURSOR_SIZE,
    .theme            = "",
    .background_image = "",
};

static int handle_key(void *userdata, const char *section, const char *name,
                      const char *value) {
    (void)userdata;

    if (*section) {
        static char last_warned[64];
        if (strcmp(last_warned, section) != 0) {
            log_warn("greeter-config: unknown section '[%s]', ignoring", section);
            snprintf(last_warned, sizeof(last_warned), "%s", section);
        }
        return 1;
    }

    if (strcmp(name, "blank-timeout") == 0) {
        conf_parse_int("greeter-config", name, value, 86400, &g_cfg.blank_timeout);
    } else if (strcmp(name, "base-font-size") == 0) {
        conf_parse_int("greeter-config", name, value, 200, &g_cfg.base_font_size);
    } else if (strcmp(name, "login-label") == 0) {
        conf_copy_str("greeter-config", name, value, g_cfg.login_label,
                      sizeof(g_cfg.login_label));
    } else if (strcmp(name, "cursor-theme") == 0) {
        conf_copy_str("greeter-config", name, value, g_cfg.cursor_theme,
                      sizeof(g_cfg.cursor_theme));
    } else if (strcmp(name, "cursor-size") == 0) {
        conf_parse_int("greeter-config", name, value, 256, &g_cfg.cursor_size);
    } else if (strcmp(name, "theme") == 0) {
        conf_copy_str("greeter-config", name, value, g_cfg.theme, sizeof(g_cfg.theme));
    } else if (strcmp(name, "background-image") == 0) {
        conf_copy_str("greeter-config", name, value, g_cfg.background_image,
                      sizeof(g_cfg.background_image));
    } else {
        log_warn("greeter-config: unknown key '%s', ignoring", name);
    }
    return 1;
}

void greeter_config_load(const char *path) {
    int r = ini_parse(path, handle_key, NULL);
    if (r < 0)
        log_warn("greeter-config: %s not found, using defaults", path);
    else if (r > 0)
        log_warn("greeter-config: parse error in %s at line %d", path, r);
    else
        log_info("greeter-config: loaded %s", path);
}

int         greeter_config_blank_timeout(void)    { return g_cfg.blank_timeout; }
int         greeter_config_base_font_size(void)   { return g_cfg.base_font_size; }
const char *greeter_config_login_label(void)      { return g_cfg.login_label; }
const char *greeter_config_cursor_theme(void)     { return g_cfg.cursor_theme; }
int         greeter_config_cursor_size(void)      { return g_cfg.cursor_size; }
const char *greeter_config_theme(void)            { return g_cfg.theme; }
const char *greeter_config_background_image(void) { return g_cfg.background_image; }
