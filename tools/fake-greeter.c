/*
 * fake-greeter.c - a fake greeter for testing the session runner
 *
 * Sleeps for a while, then responds with hardcoded credentials and exits.
 *
 * Usage: in lib/defs.h,
 * #define HEADLESS 1
 * #define COMPOSITOR "/usr/local/bin/atrium-fake-compositor"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/ipc.h"
#include "lib/log.h"

int main(void) {
    fprintf(stderr, "Fake greeter started with PID %d\n", getpid());

    ipc_channel *ch = NULL;
    if (ipc_create_from_env(&ch) < 0) {
        log_error("failed to create IPC channel");
        return EXIT_FAILURE;
    }

    sleep(5);

    /* username "alice" + empty password, both NULL-terminated. */
    const char message[] = "alice\0\0";
    if (ipc_send(ch, message, sizeof(message)) < 0) {
        log_error("failed to send credentials");
        ipc_close(ch);
        return EXIT_FAILURE;
    }

    char result[64] = {0};
    ssize_t n = ipc_recv(ch, result, sizeof(result) - 1);
    if (n <= 0) {
        log_error("failed to read auth result");
        ipc_close(ch);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Auth result: %.*s", (int)n, result);

    ipc_close(ch);
    fprintf(stderr, "Fake greeter exiting\n");
    return (strncmp(result, "ok", 2) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
