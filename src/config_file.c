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
#define DEFAULT_RESTART_DELAY    2
#define DEFAULT_BLANK_TIMEOUT    300
#define DEFAULT_SEAT_ENUM_DELAY  5
#define MAX_IGNORE_SEATS       16
#define MAX_SEAT_NAME_LENGTH          64

static struct {
    char greeter[512];
    char compositor[512];
    char desktop[64];
    int  restart_delay;
    int  blank_timeout;
    int  seat_enum_delay;
    char ignore_seats[MAX_IGNORE_SEATS][MAX_SEAT_NAME_LENGTH];
    int  ignore_seat_count;
} g_cfg = {
    .greeter            = DEFAULT_GREETER,
    .compositor         = DEFAULT_COMPOSITOR,
    .desktop            = DEFAULT_DESKTOP,
    .restart_delay      = DEFAULT_RESTART_DELAY,
    .blank_timeout      = DEFAULT_BLANK_TIMEOUT,
    .seat_enum_delay    = DEFAULT_SEAT_ENUM_DELAY,
    .ignore_seat_count  = 0,
};

/* Parse a non-negative integer from val into *out.  Returns 1 on success,
 * 0 on failure (logs a warning).  max is inclusive. */
static int parse_nonneg_int(const char *path, int lineno, const char *key,
                            const char *val, long max, int *out)
{
    char *end;
    long v = strtol(val, &end, 10);
    if (*end != '\0' || v < 0 || v > max) {
        log_warn("config: %s:%d: invalid %s '%s', using default",
                 path, lineno, key, val);
        return 0;
    }
    *out = (int)v;
    return 1;
}

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
            parse_nonneg_int(path, lineno, key, val, 3600, &g_cfg.restart_delay);
        } else if (strcmp(key, "blank-timeout") == 0) {
            parse_nonneg_int(path, lineno, key, val, 86400, &g_cfg.blank_timeout);
        } else if (strcmp(key, "seat-enum-delay") == 0) {
            parse_nonneg_int(path, lineno, key, val, 60, &g_cfg.seat_enum_delay);
        } else if (strcmp(key, "ignore-seat") == 0) {
            if (g_cfg.ignore_seat_count >= MAX_IGNORE_SEATS) {
                log_warn("config: %s:%d: too many ignore-seat entries, ignoring '%s'",
                         path, lineno, val);
            } else {
                snprintf(g_cfg.ignore_seats[g_cfg.ignore_seat_count],
                         MAX_SEAT_NAME_LENGTH, "%s", val);
                g_cfg.ignore_seat_count++;
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
    log_debug("config: blank-timeout=%d",    g_cfg.blank_timeout);
    log_debug("config: seat-enum-delay=%d",  g_cfg.seat_enum_delay);
    for (int i = 0; i < g_cfg.ignore_seat_count; i++)
        log_debug("config: ignore-seat='%s'", g_cfg.ignore_seats[i]);
}

const char *config_greeter(void)       { return g_cfg.greeter; }
const char *config_compositor(void)    { return g_cfg.compositor; }
const char *config_desktop(void)       { return g_cfg.desktop; }
int         config_restart_delay(void) { return g_cfg.restart_delay; }
int         config_blank_timeout(void)    { return g_cfg.blank_timeout; }
int         config_seat_enum_delay(void)  { return g_cfg.seat_enum_delay; }

const char **config_ignore_seats(void)
{
    /* Build a NULL-terminated view into the stored names on each call.
     * Safe because g_cfg outlives all callers (static storage). */
    static const char *ptrs[MAX_IGNORE_SEATS + 1];
    for (int i = 0; i < g_cfg.ignore_seat_count; i++)
        ptrs[i] = g_cfg.ignore_seats[i];
    ptrs[g_cfg.ignore_seat_count] = NULL;
    return ptrs;
}
