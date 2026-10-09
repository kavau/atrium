/*
 * config-test.c - prints resolved daemon config values.
 *
 * Reads and processes a daemon config file through the greeter's config parser
 * and prints the results. Outputs both the general and the seat-specific
 * configuration, if a seat is given.
 *
 * Usage: build/atrium-config-test <conf> [seat]
 */

#include <stdio.h>
#include <stdlib.h>

#include "daemon/policy/config.h"

static void dump(const char *label) {
    printf("--- %s\n", label);
    printf("  greeter               = %s\n", config_greeter());
    printf("  compositor            = %s\n", config_compositor());
    printf("  desktop               = %s\n", config_desktop());
    printf("  session-wrapper       = %s\n", config_session_wrapper());
    printf("  power-actions         = %d\n", config_power_actions());
    printf("  allow-duplicate-login = %d\n", config_allow_duplicate_login());
    printf("  seat-discovery-delay  = %d\n", config_seat_discovery_delay());
    printf("  crash-restart-delay   = %d\n", config_crash_restart_delay());
    printf("  crash-count-limit     = %d\n", config_crash_count_limit());
    printf("  crash-window          = %d\n", config_crash_window());
    printf("  drm-backoff           = %d\n", config_drm_backoff());
    for (int i = 0; i < 4; i++) {
        char seat[16];
        snprintf(seat, sizeof(seat), "seat%d", i);
        printf("  ignored(%s)         = %d\n", seat, config_is_seat_ignored(seat));
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <conf-file> [seat]\n", argv[0]);
        return EXIT_FAILURE;
    }

    config_load_path(argv[1]);
    dump("general");

    if (argc > 2) {
        config_apply_seat_overrides(argv[2]);
        dump(argv[2]);
    }
    return EXIT_SUCCESS;
}
