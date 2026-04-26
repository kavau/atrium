#include "greeter_config.h"
#include "conf_parse.h"
#include "log.h"

#include <string.h>

#define DEFAULT_BLANK_TIMEOUT    300
#define DEFAULT_CURSOR_THEME     "Adwaita"
#define DEFAULT_CURSOR_SIZE      32
#define DEFAULT_BASE_FONT_SIZE   24

static struct {
    int  blank_timeout;
    char cursor_theme[64];
    int  cursor_size;
    int  base_font_size;
} g_cfg = {
    .blank_timeout  = DEFAULT_BLANK_TIMEOUT,
    .cursor_theme   = DEFAULT_CURSOR_THEME,
    .cursor_size    = DEFAULT_CURSOR_SIZE,
    .base_font_size = DEFAULT_BASE_FONT_SIZE,
};

static void handle_key(const char *path, int lineno,
                       const char *key, const char *val,
                       void *userdata)
{
    (void)userdata;

    if (strcmp(key, "blank-timeout") == 0) {
        conf_parse_nonneg_int(path, lineno, "greeter config", key, val,
                              86400, &g_cfg.blank_timeout);
    } else if (strcmp(key, "cursor-theme") == 0) {
        conf_copy_str(path, lineno, "greeter config", key, val,
                      g_cfg.cursor_theme, sizeof(g_cfg.cursor_theme));
    } else if (strcmp(key, "cursor-size") == 0) {
        conf_parse_nonneg_int(path, lineno, "greeter config", key, val,
                              512, &g_cfg.cursor_size);
    } else if (strcmp(key, "base-font-size") == 0) {
        conf_parse_nonneg_int(path, lineno, "greeter config", key, val,
                              256, &g_cfg.base_font_size);
    } else {
        log_warn("greeter config: %s:%d: unknown key '%s', ignoring",
                 path, lineno, key);
    }
}

void greeter_config_load(const char *path)
{
    conf_parse(path, "greeter config", handle_key, NULL);

    log_debug("greeter config: blank-timeout=%d",   g_cfg.blank_timeout);
    log_debug("greeter config: cursor-theme='%s'",  g_cfg.cursor_theme);
    log_debug("greeter config: cursor-size=%d",     g_cfg.cursor_size);
    log_debug("greeter config: base-font-size=%d",  g_cfg.base_font_size);
}

int         greeter_config_blank_timeout(void)  { return g_cfg.blank_timeout; }
const char *greeter_config_cursor_theme(void)   { return g_cfg.cursor_theme; }
int         greeter_config_cursor_size(void)    { return g_cfg.cursor_size; }
int         greeter_config_base_font_size(void) { return g_cfg.base_font_size; }
