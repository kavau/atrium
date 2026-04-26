#include "greeter_config.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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

static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

static int parse_nonneg_int(const char *path, int lineno, const char *key,
                            const char *val, long max, int *out)
{
    char *end;
    long v = strtol(val, &end, 10);
    if (*end != '\0' || v < 0 || v > max) {
        log_warn("greeter config: %s:%d: invalid %s '%s', using default",
                 path, lineno, key, val);
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* Copy val into dst (size dst_size), logging a warning if it had to be
 * truncated. */
static void copy_str(const char *path, int lineno, const char *key,
                     const char *val, char *dst, size_t dst_size)
{
    size_t vlen = strlen(val);
    if (vlen >= dst_size) {
        log_warn("greeter config: %s:%d: %s value too long (%zu bytes, max %zu), truncating",
                 path, lineno, key, vlen, dst_size - 1);
    }
    snprintf(dst, dst_size, "%s", val);
}

void greeter_config_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT)
            log_info("greeter config: %s not found, using defaults", path);
        else
            log_warn("greeter config: cannot open %s: %s", path, strerror(errno));
        return;
    }
    log_info("greeter config: loading %s", path);

    char line[512];
    int  lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        rtrim(line);

        char *p = line;
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        char *eq = strchr(p, '=');
        if (!eq) {
            log_warn("greeter config: %s:%d: missing '=', skipping", path, lineno);
            continue;
        }
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        rtrim(key);
        while (*val && isspace((unsigned char)*val))
            val++;

        if (strcmp(key, "blank-timeout") == 0) {
            parse_nonneg_int(path, lineno, key, val, 86400, &g_cfg.blank_timeout);
        } else if (strcmp(key, "cursor-theme") == 0) {
            copy_str(path, lineno, key, val, g_cfg.cursor_theme, sizeof(g_cfg.cursor_theme));
        } else if (strcmp(key, "cursor-size") == 0) {
            parse_nonneg_int(path, lineno, key, val, 512, &g_cfg.cursor_size);
        } else if (strcmp(key, "base-font-size") == 0) {
            parse_nonneg_int(path, lineno, key, val, 256, &g_cfg.base_font_size);
        } else {
            log_warn("greeter config: %s:%d: unknown key '%s', ignoring", path, lineno, key);
        }
    }

    fclose(f);

    log_debug("greeter config: blank-timeout=%d",   g_cfg.blank_timeout);
    log_debug("greeter config: cursor-theme='%s'",  g_cfg.cursor_theme);
    log_debug("greeter config: cursor-size=%d",     g_cfg.cursor_size);
    log_debug("greeter config: base-font-size=%d",  g_cfg.base_font_size);
}

int         greeter_config_blank_timeout(void)  { return g_cfg.blank_timeout; }
const char *greeter_config_cursor_theme(void)   { return g_cfg.cursor_theme; }
int         greeter_config_cursor_size(void)    { return g_cfg.cursor_size; }
int         greeter_config_base_font_size(void) { return g_cfg.base_font_size; }
