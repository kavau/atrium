#include "config_file.h"
#include "conf_parse.h"
#include "log.h"

#include <string.h>

#ifndef ATRIUM_GREETER_PATH
#  define ATRIUM_GREETER_PATH "/usr/libexec/atrium-greeter"
#endif

#define DEFAULT_GREETER        "/usr/bin/cage -s -- " ATRIUM_GREETER_PATH
#define DEFAULT_COMPOSITOR     "sway"
#define DEFAULT_DESKTOP        "sway"
#define DEFAULT_RESTART_DELAY    2
#define DEFAULT_SEAT_ENUM_DELAY  5
#define MAX_IGNORE_SEATS         16
#define MAX_SEAT_NAME_LENGTH     64
#define MAX_PASSWORDLESS_USERS   16
#define MAX_USERNAME_LENGTH      255

static struct {
    char greeter[512];
    char compositor[512];
    char desktop[64];
    int  restart_delay;
    int  seat_enum_delay;
    char ignore_seats[MAX_IGNORE_SEATS][MAX_SEAT_NAME_LENGTH];
    int  ignore_seat_count;
    char passwordless_users[MAX_PASSWORDLESS_USERS][MAX_USERNAME_LENGTH];
    int  passwordless_user_count;
} g_cfg = {
    .greeter                 = DEFAULT_GREETER,
    .compositor              = DEFAULT_COMPOSITOR,
    .desktop                 = DEFAULT_DESKTOP,
    .restart_delay           = DEFAULT_RESTART_DELAY,
    .seat_enum_delay         = DEFAULT_SEAT_ENUM_DELAY,
    .ignore_seat_count       = 0,
    .passwordless_user_count = 0,
};

/* Append val to a fixed 2-D char array (list[max][item_size]), logging a
 * warning and doing nothing if the list is full, or truncating with a
 * warning if val is longer than item_size - 1. */
static void append_strlist(const char *path, int lineno, const char *key,
                           const char *val,
                           char *list, int *count, int max, size_t item_size)
{
    if (*count >= max) {
        log_warn("config: %s:%d: too many %s entries, ignoring '%s'",
                 path, lineno, key, val);
        return;
    }
    size_t vlen = strlen(val);
    if (vlen >= item_size) {
        log_warn("config: %s:%d: %s value too long (%zu bytes, max %zu), truncating",
                 path, lineno, key, vlen, item_size - 1);
    }
    snprintf(list + (size_t)(*count) * item_size, item_size, "%s", val);
    (*count)++;
}

static void handle_key(const char *path, int lineno,
                       const char *key, const char *val,
                       void *userdata)
{
    (void)userdata;

    if (strcmp(key, "greeter") == 0) {
        conf_copy_str(path, lineno, "config", key, val,
                      g_cfg.greeter, sizeof(g_cfg.greeter));
    } else if (strcmp(key, "compositor") == 0) {
        conf_copy_str(path, lineno, "config", key, val,
                      g_cfg.compositor, sizeof(g_cfg.compositor));
    } else if (strcmp(key, "desktop") == 0) {
        conf_copy_str(path, lineno, "config", key, val,
                      g_cfg.desktop, sizeof(g_cfg.desktop));
    } else if (strcmp(key, "restart-delay") == 0) {
        conf_parse_nonneg_int(path, lineno, "config", key, val,
                              3600, &g_cfg.restart_delay);
    } else if (strcmp(key, "seat-enum-delay") == 0) {
        conf_parse_nonneg_int(path, lineno, "config", key, val,
                              60, &g_cfg.seat_enum_delay);
    } else if (strcmp(key, "ignore-seat") == 0) {
        append_strlist(path, lineno, key, val,
                       g_cfg.ignore_seats[0], &g_cfg.ignore_seat_count,
                       MAX_IGNORE_SEATS, MAX_SEAT_NAME_LENGTH);
    } else if (strcmp(key, "passwordless-user") == 0) {
        append_strlist(path, lineno, key, val,
                       g_cfg.passwordless_users[0], &g_cfg.passwordless_user_count,
                       MAX_PASSWORDLESS_USERS, MAX_USERNAME_LENGTH);
    } else {
        log_warn("config: %s:%d: unknown key '%s', ignoring", path, lineno, key);
    }
}

void config_load(const char *path)
{
    conf_parse(path, "config", handle_key, NULL);

    log_debug("config: greeter='%s'",        g_cfg.greeter);
    log_debug("config: compositor='%s'",     g_cfg.compositor);
    log_debug("config: desktop='%s'",        g_cfg.desktop);
    log_debug("config: restart-delay=%d",    g_cfg.restart_delay);
    log_debug("config: seat-enum-delay=%d",  g_cfg.seat_enum_delay);
    for (int i = 0; i < g_cfg.ignore_seat_count; i++)
        log_debug("config: ignore-seat='%s'", g_cfg.ignore_seats[i]);
    for (int i = 0; i < g_cfg.passwordless_user_count; i++)
        log_debug("config: passwordless-user='%s'", g_cfg.passwordless_users[i]);
}

const char *config_greeter(void)         { return g_cfg.greeter; }
const char *config_compositor(void)      { return g_cfg.compositor; }
const char *config_desktop(void)         { return g_cfg.desktop; }
int         config_restart_delay(void)   { return g_cfg.restart_delay; }
int         config_seat_enum_delay(void) { return g_cfg.seat_enum_delay; }

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

const char **config_passwordless_users(void)
{
    static const char *ptrs[MAX_PASSWORDLESS_USERS + 1];
    for (int i = 0; i < g_cfg.passwordless_user_count; i++)
        ptrs[i] = g_cfg.passwordless_users[i];
    ptrs[g_cfg.passwordless_user_count] = NULL;
    return ptrs;
}
