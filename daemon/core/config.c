#include "config.h"

#include <string.h>

#include "ini.h"
#include "lib/conf_helpers.h"
#include "lib/log.h"

#ifndef ATRIUM_GREETER_PATH
#define ATRIUM_GREETER_PATH "/usr/libexec/atrium-greeter"
#endif

#define DEFAULT_GREETER "/usr/bin/cage -s -- " ATRIUM_GREETER_PATH
#define DEFAULT_SEAT_DISCOVERY_DELAY 2000 /* ms */
#define DEFAULT_CRASH_RESTART_DELAY  1000 /* ms */
#define DEFAULT_CRASH_COUNT_LIMIT       5
#define DEFAULT_CRASH_WINDOW           60 /* seconds */
#define MAX_IGNORE_SEATS 16
#define MAX_SEAT_NAME_LEN 64

static struct {
    char greeter[512];
    char compositor[512];
    char desktop[64];
    int seat_discovery_delay;
    int crash_restart_delay;
    int crash_count_limit;
    int crash_window; /* seconds */
    char ignore_seats[MAX_IGNORE_SEATS][MAX_SEAT_NAME_LEN];
    int ignore_seat_count;
} g_cfg = {
    .greeter = DEFAULT_GREETER,
    .compositor = "",
    .desktop = "",
    .seat_discovery_delay = DEFAULT_SEAT_DISCOVERY_DELAY,
    .crash_restart_delay  = DEFAULT_CRASH_RESTART_DELAY,
    .crash_count_limit    = DEFAULT_CRASH_COUNT_LIMIT,
    .crash_window         = DEFAULT_CRASH_WINDOW,
    .ignore_seat_count = 0,
};

static int handle_key(void *userdata, const char *section, const char *name, const char *value) {
    (void)userdata;

    if (*section) {
        static char last_warned[64];
        if (strcmp(last_warned, section) != 0) {
            log_warn("config: unknown section '[%s]', ignoring", section);
            snprintf(last_warned, sizeof(last_warned), "%s", section);
        }
        return 1;
    }

    if (strcmp(name, "greeter") == 0) {
        conf_copy_str("config", name, value, g_cfg.greeter, sizeof(g_cfg.greeter));
    } else if (strcmp(name, "compositor") == 0) {
        conf_copy_str("config", name, value, g_cfg.compositor, sizeof(g_cfg.compositor));
    } else if (strcmp(name, "desktop") == 0) {
        conf_copy_str("config", name, value, g_cfg.desktop, sizeof(g_cfg.desktop));
    } else if (strcmp(name, "seat-discovery-delay") == 0) {
        conf_parse_int("config", name, value, 60000, &g_cfg.seat_discovery_delay);
    } else if (strcmp(name, "crash-restart-delay") == 0) {
        conf_parse_int("config", name, value, 60000, &g_cfg.crash_restart_delay);
    } else if (strcmp(name, "crash-count-limit") == 0) {
        conf_parse_int("config", name, value, 100, &g_cfg.crash_count_limit);
    } else if (strcmp(name, "crash-window") == 0) {
        conf_parse_int("config", name, value, 3600, &g_cfg.crash_window);
    } else if (strcmp(name, "ignore-seat") == 0) {
        conf_append_strlist("config", name, value, g_cfg.ignore_seats[0], &g_cfg.ignore_seat_count,
                            MAX_IGNORE_SEATS, MAX_SEAT_NAME_LEN);
    } else {
        log_warn("config: unknown key '%s', ignoring", name);
    }
    return 1;
}

void config_load(const char *path) {
    int r = ini_parse(path, handle_key, NULL);
    if (r < 0)
        log_warn("config: %s not found, using defaults", path);
    else if (r > 0)
        log_warn("config: parse error in %s at line %d", path, r);
    else
        log_info("config: loaded %s", path);
}

const char *config_greeter(void) { return g_cfg.greeter; }
const char *config_compositor(void) { return g_cfg.compositor; }
const char *config_desktop(void) { return g_cfg.desktop; }
int config_seat_discovery_delay(void) { return g_cfg.seat_discovery_delay; }
int config_crash_restart_delay(void)  { return g_cfg.crash_restart_delay; }
int config_crash_count_limit(void)    { return g_cfg.crash_count_limit; }
int config_crash_window(void)         { return g_cfg.crash_window; }

int config_is_seat_ignored(const char *seat_id) {
    for (int i = 0; i < g_cfg.ignore_seat_count; i++)
        if (strcmp(g_cfg.ignore_seats[i], seat_id) == 0)
            return 1;
    return 0;
}
