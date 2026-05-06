/*
 * A simple text-based greeter.
 *
 * Runs inside cage + foot.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/log.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    char username[LINE_MAX];
    printf("Username: ");
    if (!fgets(username, sizeof(username), stdin)) {
        log_error("failed to read username\n");
        return 1;
    }
    // Remove trailing newline
    username[strcspn(username, "\n")] = '\0';

    const char *password = getpass("Password: ");
    if (!password) {
        log_error("failed to read password\n");
        return 1;
    }

    printf("Press Enter to log in...\n");
    getchar();

    return EXIT_SUCCESS;
}
