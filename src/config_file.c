#include "config_file.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ATRIUM_GREETER_PATH
#  define ATRIUM_GREETER_PATH "/usr/libexec/atrium-greeter"
#endif

#define DEFAULT_GREETER        "/usr/bin/cage -- " ATRIUM_GREETER_PATH
#define DEFAULT_COMPOSITOR     "sway"
#define DEFAULT_DESKTOP        "sway"
#define DEFAULT_RESTART_DELAY  2

static struct {
    char greeter[512];
    char compositor[512];
    char desktop[64];
    int  restart_delay;
} g_cfg = {
    .greeter       = DEFAULT_GREETER,
    .compositor    = DEFAULT_COMPOSITOR,
    .desktop       = DEFAULT_DESKTOP,
    .restart_delay = DEFAULT_RESTART_DELAY,
};

static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

void config_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT)
            log_info("config: %s not found, using defaults", path);
        else
            log_warn("config: cannot open %s: %s", path, strerror(errno));
        return;
    }
    log_info("config: loading %s", path);

    char line[1024];
    int  lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        rtrim(line);

        /* Skip leading whitespace, blank lines, and comments. */
        char *p = line;
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        /* Split on the first '='. */
        char *eq = strchr(p, '=');
        if (!eq) {
            log_warn("config: %s:%d: missing '=', skipping", path, lineno);
            continue;
        }
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        rtrim(key);
        while (*val && isspace((unsigned char)*val))
            val++;

        if (strcmp(key, "greeter") == 0) {
            snprintf(g_cfg.greeter, sizeof(g_cfg.greeter), "%s", val);
        } else if (strcmp(key, "compositor") == 0) {
            snprintf(g_cfg.compositor, sizeof(g_cfg.compositor), "%s", val);
        } else if (strcmp(key, "desktop") == 0) {
            snprintf(g_cfg.desktop, sizeof(g_cfg.desktop), "%s", val);
        } else if (strcmp(key, "restart-delay") == 0) {
            char *end;
            long v = strtol(val, &end, 10);
            if (*end != '\0' || v < 0 || v > 3600) {
                log_warn("config: %s:%d: invalid restart-delay '%s', using default",
                         path, lineno, val);
            } else {
                g_cfg.restart_delay = (int)v;
            }
        } else {
            log_warn("config: %s:%d: unknown key '%s', ignoring", path, lineno, key);
        }
    }

    fclose(f);

    log_debug("config: greeter='%s'",        g_cfg.greeter);
    log_debug("config: compositor='%s'",     g_cfg.compositor);
    log_debug("config: desktop='%s'",        g_cfg.desktop);
    log_debug("config: restart-delay=%d",    g_cfg.restart_delay);
}

const char *config_greeter(void)       { return g_cfg.greeter; }
const char *config_compositor(void)    { return g_cfg.compositor; }
const char *config_desktop(void)       { return g_cfg.desktop; }
int         config_restart_delay(void) { return g_cfg.restart_delay; }
