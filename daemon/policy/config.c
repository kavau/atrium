#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini.h"
#include "lib/conf_helpers.h"
#include "lib/defs.h"
#include "lib/log.h"

#ifndef ATRIUM_GREETER_PATH
#define ATRIUM_GREETER_PATH "/usr/libexec/atrium-greeter"
#endif

#ifndef ATRIUM_SESSION_WRAPPER_PATH
#define ATRIUM_SESSION_WRAPPER_PATH "/usr/share/atrium/session-wrapper"
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
    char session_wrapper[512];
    char desktop[64];
    int  seat_discovery_delay;
    int  crash_restart_delay;
    int  crash_count_limit;
    int  crash_window; /* seconds */
    int  drm_backoff;  /* ms */
    int  allow_duplicate_login;
    int  power_actions;
    char ignore_seats[MAX_IGNORE_SEATS][MAX_SEAT_NAME_LEN];
    int  ignore_seat_count;
} config_data;

/* Compiled-in defaults used for missing keys or when config file is absent. */
static const config_data default_config = {
    .greeter = DEFAULT_GREETER,
    .compositor = "",
    .desktop = "",
    .session_wrapper = ATRIUM_SESSION_WRAPPER_PATH,
    .seat_discovery_delay = DEFAULT_SEAT_DISCOVERY_DELAY,
    .crash_restart_delay = DEFAULT_CRASH_RESTART_DELAY,
    .crash_count_limit = DEFAULT_CRASH_COUNT_LIMIT,
    .crash_window = DEFAULT_CRASH_WINDOW,
    .drm_backoff = DEFAULT_DRM_BACKOFF,
    .allow_duplicate_login = 0,
    .power_actions = 1,
    .ignore_seat_count = 0,
};

/* Keys that can be overridden in a seat section. */
static const char *const overridable[] = {
    "greeter", "compositor", "desktop", "session-wrapper", "power-actions",
};

static config_data g_cfg = default_config;

static char *g_raw; /* Retained copy of the config file contents. */
static char  last_reported_section[64] = "";

static int key_is_seat_overridable(const char *name) {
    for (size_t i = 0; i < sizeof(overridable) / sizeof(*overridable); i++)
        if (strcmp(name, overridable[i]) == 0)
            return 1;
    return 0;
}

struct parse_ctx {
    config_data *cfg;
    const char  *seat; /* seat whose section applies, or NULL */
};

static int handle_key(void *userdata, const char *section, const char *name, const char *value) {
    struct parse_ctx *ctx = userdata;
    config_data      *cfg = ctx->cfg;

    /* Keys in our seat's section are treated exactly like keys outside any
    section, so a later seat-specific entry overrides an earlier general one. */
    if (*section) {
        bool is_seat = conf_is_seat_section(section, ctx->seat);
        /* Report each section only once, rather than once per key. */
        if (strcmp(last_reported_section, section) != 0) {
            snprintf(last_reported_section, sizeof(last_reported_section), "%s", section);
            log_info("config: %s section '[%s]'", is_seat ? "applying" : "ignoring", section);
        }
        if (!is_seat)
            return 1;
        if (!key_is_seat_overridable(name)) {
            log_warn("config: '%s' is not a key that can be set per seat, ignoring it in '[%s]'",
                     name, section);
            return 1;
        }
    }

    if (strcmp(name, "greeter") == 0) {
        conf_copy_str("config", name, value, cfg->greeter, sizeof(cfg->greeter));
    } else if (strcmp(name, "compositor") == 0) {
        conf_copy_str("config", name, value, cfg->compositor, sizeof(cfg->compositor));
    } else if (strcmp(name, "session-wrapper") == 0) {
        conf_copy_str("config", name, value, cfg->session_wrapper, sizeof(cfg->session_wrapper));
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
    } else if (strcmp(name, "power-actions") == 0) {
        conf_parse_bool("config", name, value, &cfg->power_actions);
    } else if (strcmp(name, "ignore-seat") == 0) {
        conf_append_strlist("config", name, value, cfg->ignore_seats[0], &cfg->ignore_seat_count,
                            MAX_IGNORE_SEATS, MAX_SEAT_NAME_LEN);
    } else {
        log_warn("config: unknown key '%s', ignoring", name);
    }
    return 1;
}

/* Parse config file contents into *out. Returns 0 on success, the failing line
number otherwise. */
static int parse_config_text(const char *text, const char *seat, config_data *out) {
    struct parse_ctx ctx = {.cfg = out, .seat = seat};
    *out = default_config;
    last_reported_section[0] = '\0'; /* reset the per-section report */
    return ini_parse_string(text, handle_key, &ctx);
}

void config_load(void) { config_load_path(CONFIG_PATH); }

void config_load_path(const char *path) {
    free(g_raw);
    g_raw = conf_read_file(path);
    if (!g_raw) {
        log_warn("config: %s not readable, using defaults", path);
        g_cfg = default_config;
        return;
    }

    /* Parse into a fresh copy seeded with defaults, then update atomically.
    This keeps reloads idempotent. Seat sections are skipped here. */
    config_data new_cfg;
    int         r = parse_config_text(g_raw, NULL, &new_cfg);
    if (r > 0) {
        log_warn("config: parse error in %s at line %d, keeping previous config", path, r);
    } else {
        log_info("config: loaded %s", path);
        g_cfg = new_cfg;
    }
}

void config_apply_seat_overrides(const char *seat) {
    if (!seat || !*seat)
        return; /* nothing to do */
    if (!g_raw) {
        log_debug("config: no config file loaded, no seat overrides for '%s'", seat);
        return;
    }

    /* Re-parse the stored copy instead of re-reading the file to guarantee the
    runner sees the same configuration as the daemon. */
    config_data seat_cfg;
    int         r = parse_config_text(g_raw, seat, &seat_cfg);
    if (r > 0) {
        log_warn("config: parse error at line %d, keeping general config for seat '%s'", r, seat);
        return;
    }
    g_cfg = seat_cfg;
}

const char *config_greeter(void) { return g_cfg.greeter; }
const char *config_compositor(void) { return g_cfg.compositor; }
const char *config_desktop(void) { return g_cfg.desktop; }
const char *config_session_wrapper(void) { return g_cfg.session_wrapper; }
int         config_seat_discovery_delay(void) { return g_cfg.seat_discovery_delay; }
int         config_crash_restart_delay(void) { return g_cfg.crash_restart_delay; }
int         config_crash_count_limit(void) { return g_cfg.crash_count_limit; }
int         config_crash_window(void) { return g_cfg.crash_window; }
int         config_drm_backoff(void) { return g_cfg.drm_backoff; }
int         config_allow_duplicate_login(void) { return g_cfg.allow_duplicate_login; }
int         config_power_actions(void) { return g_cfg.power_actions; }

int config_is_seat_ignored(const char *seat_id) {
    for (int i = 0; i < g_cfg.ignore_seat_count; i++)
        if (strcmp(g_cfg.ignore_seats[i], seat_id) == 0)
            return 1;
    return 0;
}
