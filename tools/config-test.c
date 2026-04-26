/*
 * config-test.c — standalone configuration file test for atrium.
 *
 * Loads /etc/atrium.conf and /etc/atrium-greeter.conf (or paths given on the
 * command line) via the same code paths used by the daemon and greeter, then
 * prints every resolved value.
 *
 * Usage:
 *   ./build/atrium-config-test
 *   ./build/atrium-config-test /path/to/atrium.conf /path/to/atrium-greeter.conf
 *
 * Exit codes: always 0 — config loading falls back to defaults on error.
 */

#include <stdio.h>

#include "config_file.h"
#include "greeter_config.h"

int main(int argc, char **argv)
{
    const char *daemon_conf  = argc > 1 ? argv[1] : "/etc/atrium.conf";
    const char *greeter_conf = argc > 2 ? argv[2] : "/etc/atrium-greeter.conf";

    config_load(daemon_conf);
    greeter_config_load(greeter_conf);

    printf("\n--- atrium.conf (%s) ---\n", daemon_conf);
    printf("greeter        = %s\n", config_greeter());
    printf("compositor     = %s\n", config_compositor());
    printf("desktop        = %s\n", config_desktop());
    printf("restart-delay  = %d\n", config_restart_delay());
    printf("seat-enum-delay= %d\n", config_seat_enum_delay());

    const char **seats = config_ignore_seats();
    if (!seats[0])
        printf("ignore-seat    = (none)\n");
    for (int i = 0; seats[i]; i++)
        printf("ignore-seat    = %s\n", seats[i]);

    const char **users = config_passwordless_users();
    if (!users[0])
        printf("passwordless-user = (none)\n");
    for (int i = 0; users[i]; i++)
        printf("passwordless-user = %s\n", users[i]);

    printf("\n--- atrium-greeter.conf (%s) ---\n", greeter_conf);
    printf("blank-timeout  = %d\n", greeter_config_blank_timeout());
    printf("cursor-theme   = %s\n", greeter_config_cursor_theme());
    printf("cursor-size    = %d\n", greeter_config_cursor_size());
    printf("base-font-size = %d\n", greeter_config_base_font_size());

    return 0;
}
