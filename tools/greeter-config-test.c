/*
 * greeter-config-test.c - prints resolved greeter config values for a seat
 *
 * Reads and processes a greeter config file through the greeter's config parser
 * and prints the results.
 *
 * Usage: XDG_SEAT=seat1 build/atrium-greeter-config-test <conf> [username ...]
 */

#include <stdio.h>
#include <stdlib.h>

#include "greeter/config.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <conf-file> [username ...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *seat = getenv("XDG_SEAT");
    printf("XDG_SEAT=%s\n", seat && *seat ? seat : "(unset)");

    greeter_config_load(argv[1]);

    printf("blank-timeout    = %d\n", greeter_config_blank_timeout());
    printf("base-font-size   = %d\n", greeter_config_base_font_size());
    printf("login-label      = %s\n", greeter_config_login_label());
    printf("cursor-theme     = %s\n", greeter_config_cursor_theme());
    printf("cursor-size      = %d\n", greeter_config_cursor_size());
    printf("theme            = %s\n", greeter_config_theme());
    printf("background-image = %s\n", greeter_config_background_image());

    for (int i = 2; i < argc; i++)
        printf("hidden(%s) = %s\n", argv[i], greeter_config_is_hidden_user(argv[i]) ? "yes" : "no");

    return EXIT_SUCCESS;
}
