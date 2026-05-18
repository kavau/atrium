/*
 * fake-greeter.c - a fake greeter for testing the session runner
 *
 * Sends three credential attempts in sequence to exercise the auth loop:
 *   1. malformed message (no NUL terminator) -> expect "fail:invalid credentials"
 *   2. nonexistent user                      -> expect "fail:authentication failed"
 *   3. correct credentials                   -> expect "ok"
 *
 * Usage: in lib/defs.h,
 * #define HEADLESS 1
 * #define COMPOSITOR "/usr/local/bin/atrium-fake-compositor"
 *
 * Set ATRIUM_LOG_STDERR=1 to see child output in the terminal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/ipc.h"
#include "lib/log.h"

#define USERNAME "alice"
#define PASSWORD ""

static int send_and_recv(ipc_channel *ch, const void *msg, size_t len, const char *desc,
                         const char *expect_prefix) {
    fprintf(stderr, "fake-greeter: sending %s\n", desc);
    if (ipc_send(ch, msg, len) < 0) {
        log_error("fake-greeter: ipc_send failed (%s)", desc);
        return -1;
    }
    char result[64] = {0};
    ssize_t n = ipc_recv(ch, result, sizeof(result) - 1);
    if (n <= 0) {
        log_error("fake-greeter: ipc_recv failed (%s)", desc);
        return -1;
    }
    fprintf(stderr, "fake-greeter: result: %.*s", (int)n, result);
    if (strncmp(result, expect_prefix, strlen(expect_prefix)) != 0) {
        log_error("fake-greeter: unexpected result for %s (expected '%s')", desc, expect_prefix);
        return -1;
    }
    return 0;
}

int main(void) {
    fprintf(stderr, "Fake greeter started with PID %d\n", getpid());

    ipc_channel *ch = NULL;
    if (ipc_create_from_env(&ch) < 0) {
        log_error("failed to create IPC channel");
        return EXIT_FAILURE;
    }

    sleep(2);

    /* 1. Malformed: no NUL terminator, so parse_credentials fails. */
    const char malformed[] = "notvalid";
    if (send_and_recv(ch, malformed, sizeof(malformed) - 1, "malformed credentials", "fail:") < 0) {
        ipc_close(ch);
        return EXIT_FAILURE;
    }

    sleep(2);

    /* 2. Nonexistent user: well-formed but PAM will reject it. */
    const char bad_user[] = "nonexistent\0password\0";
    if (send_and_recv(ch, bad_user, sizeof(bad_user), "nonexistent user", "fail:") < 0) {
        ipc_close(ch);
        return EXIT_FAILURE;
    }

    sleep(2);

    /* 3. Correct credentials. */
    const char good[] = USERNAME "\0" PASSWORD "\0";
    if (send_and_recv(ch, good, sizeof(good), "correct credentials", "ok") < 0) {
        ipc_close(ch);
        return EXIT_FAILURE;
    }

    ipc_close(ch);
    fprintf(stderr, "Fake greeter exiting\n");
    return EXIT_SUCCESS;
}
