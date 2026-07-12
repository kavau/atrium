#include "config.h"

#include <string.h>

#include "ini.h"
#include "lib/conf_helpers.h"
#include "lib/defs.h"
#include "lib/log.h"

#ifndef ATRIUM_GREETER_PATH
#define ATRIUM_GREETER_PATH "/usr/libexec/atrium-greeter"
#endif

#define DEFAULT_GREETER              "/usr/bin/cage -s -- " ATRIUM_GREETER_PATH
#define DEFAULT_SEAT_DISCOVERY_DELAY 0    /* ms; 0 = disabled */
#define DEFAULT_CRASH_RESTART_DELAY  1000 /* ms */
#define DEFAULT_CRASH_COUNT_LIMIT    5
#define DEFAULT_CRASH_WINDOW         60  /* seconds */
#define DEFAULT_DRM_BACKOFF          500 /* ms */
#define MAX_IGNORE_SEATS             16
#define MAX_SEAT_NAME_LEN            64

typedef struct config_data {
    char greeter[512];
    char compositor[512];
    char desktop[64];
    int  seat_discovery_delay;
    int  crash_restart_delay;
    int  crash_count_limit;
    int  crash_window; /* seconds */
    int  drm_backoff;  /* ms */
    int  allow_duplicate_login;
    char ignore_seats[MAX_IGNORE_SEATS][MAX_SEAT_NAME_LEN];
    int  ignore_seat_count;
} config_data;

/* Compiled-in defaults used for missing keys or when config file is absent. */
static const config_data default_config = {
    .greeter = DEFAULT_GREETER,
    .compositor = "",
    .desktop = "",
    .seat_discovery_delay = DEFAULT_SEAT_DISCOVERY_DELAY,
    .crash_restart_delay = DEFAULT_CRASH_RESTART_DELAY,
    .crash_count_limit = DEFAULT_CRASH_COUNT_LIMIT,
    .crash_window = DEFAULT_CRASH_WINDOW,
    .drm_backoff = DEFAULT_DRM_BACKOFF,
    .allow_duplicate_login = 0,
    .ignore_seat_count = 0,
};

static config_data g_cfg = default_config;
static char        last_warned_section[64] = "";

static int handle_key(void *userdata, const char *section, const char *name, const char *value) {
    config_data *cfg = userdata;

    if (*section) {
        if (strcmp(last_warned_section, section) != 0) {
            log_warn("config: unknown section '[%s]', ignoring", section);
            snprintf(last_warned_section, sizeof(last_warned_section), "%s", section);
        }
        return 1;
    }

    if (strcmp(name, "greeter") == 0) {
        conf_copy_str("config", name, value, cfg->greeter, sizeof(cfg->greeter));
    } else if (strcmp(name, "compositor") == 0) {
        conf_copy_str("config", name, value, cfg->compositor, sizeof(cfg->compositor));
    } else if (strcmp(name, "desktop") == 0) {
        conf_copy_str("config", name, value, cfg->desktop, sizeof(cfg->desktop));
    } else if (strcmp(name, "seat-discovery-delay") == 0) {
        conf_parse_int("config", name, value, 60000, &cfg->seat_discovery_delay);
    } else if (strcmp(name, "crash-restart-delay") == 0) {
        conf_parse_int("config", name, value, 60000, &cfg->crash_restart_delay);
    } else if (strcmp(name, "crash-count-limit") == 0) {
        conf_parse_int("config", name, value, 100, &cfg->crash_count_limit);
    } else if (strcmp(name, "crash-window") == 0) {
        conf_parse_int("config", name, value, 3600, &cfg->crash_window);
    } else if (strcmp(name, "drm-backoff") == 0) {
        conf_parse_int("config", name, value, 60000, &cfg->drm_backoff);
    } else if (strcmp(name, "allow-duplicate-login") == 0) {
        conf_parse_bool("config", name, value, &cfg->allow_duplicate_login);
    } else if (strcmp(name, "ignore-seat") == 0) {
        conf_append_strlist("config", name, value, cfg->ignore_seats[0], &cfg->ignore_seat_count,
                            MAX_IGNORE_SEATS, MAX_SEAT_NAME_LEN);
    } else {
        log_warn("config: unknown key '%s', ignoring", name);
    }
    return 1;
}

void config_load(void) {
    /* Parse into a fresh copy seeded with defaults, then update atomically.
    This keeps reloads idempotent. */
    config_data new_cfg = default_config;
    last_warned_section[0] = '\0'; /* reset unknown section warning */
    int r = ini_parse(CONFIG_PATH, handle_key, &new_cfg);
    if (r < 0) {
        log_warn("config: %s not found, using defaults", CONFIG_PATH);
        g_cfg = default_config;
    } else if (r > 0) {
        log_warn("config: parse error in %s at line %d, keeping previous config", CONFIG_PATH, r);
    } else {
        log_info("config: loaded %s", CONFIG_PATH);
        g_cfg = new_cfg;
    }
}

const char *config_greeter(void) { return g_cfg.greeter; }
const char *config_compositor(void) { return g_cfg.compositor; }
const char *config_desktop(void) { return g_cfg.desktop; }
int         config_seat_discovery_delay(void) { return g_cfg.seat_discovery_delay; }
int         config_crash_restart_delay(void) { return g_cfg.crash_restart_delay; }
int         config_crash_count_limit(void) { return g_cfg.crash_count_limit; }
int         config_crash_window(void) { return g_cfg.crash_window; }
int         config_drm_backoff(void) { return g_cfg.drm_backoff; }
int         config_allow_duplicate_login(void) { return g_cfg.allow_duplicate_login; }

int config_is_seat_ignored(const char *seat_id) {
    for (int i = 0; i < g_cfg.ignore_seat_count; i++)
        if (strcmp(g_cfg.ignore_seats[i], seat_id) == 0)
            return 1;
    return 0;
}
