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

    const char *message = "family\0\0";
    if (ipc_send(ch, message, 19) < 0) {
        log_error("failed to send message over IPC channel");
        ipc_close(ch);
        return EXIT_FAILURE;
    }
    ipc_close(ch);

    fprintf(stderr, "Fake greeter exiting\n");
    return 0;
}