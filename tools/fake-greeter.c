/*
 * fake-greeter.c - a fake greeter for testing the session runner
 *
 * Sends a sequence of messages to exercise the auth loop:
 *   1. untagged message (no NUL terminator)  -> expect "fail:invalid message"
 *   2. unknown tag                           -> expect "fail:invalid message"
 *   3. malformed body (tag but no NUL)       -> expect "fail:invalid credentials"
 *   4. nonexistent user                      -> expect "fail:authentication failed"
 *   5. correct credentials                   -> expect "ok"
 *
 * Usage:
 *
 * Run this from a text console. Needs an 'alice' account that has passwordless
 * login enabled (or adjust USERNAME / PASSWORD below).
 *
 * 1. `#define HEADLESS 1` in lib/defs.h to skip VT allocation.
 * 2. In /etc/atrium.conf:
 *    - Point both children at the fakes:
 *        greeter    = <builddir>/atrium-fake-greeter
 *        compositor = <builddir>/atrium-fake-compositor
 *      Make sure the builddir is accessible to the authenticating user.
 *    - Leave exactly one non-seat0 seat active:
 *        ignore-seat = seat0
 *        ignore-seat = seat2  # etc.
 * 3. Start the daemon (must be the only instance):
 *      ninja -C build
 *      sudo systemctl stop atrium
 *      sudo env ATRIUM_LOG_STDERR=1 build/atrium
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/defs.h"
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
    char    result[64] = {0};
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

    char    msg[256];
    ssize_t mlen;

    /* 1. No NUL terminator. */
    const char untagged[] = "notvalid";
    if (send_and_recv(ch, untagged, sizeof(untagged) - 1, "untagged message", "fail:") < 0) {
        ipc_close(ch);
        return EXIT_FAILURE;
    }
    sleep(2);

    /* 2. Unknown tag. */
    mlen = ipc_msg_build3(msg, sizeof(msg), "bogus", USERNAME, PASSWORD, "");
    if (mlen < 0 || send_and_recv(ch, msg, (size_t)mlen, "unknown tag", "fail:") < 0) {
        ipc_close(ch);
        return EXIT_FAILURE;
    }
    sleep(2);

    /* 3. Tagged, but the body has no NUL terminator. */
    const char bad_body[] = IPC_TYPE_CRED "\0notvalid";
    if (send_and_recv(ch, bad_body, sizeof(bad_body) - 1, "malformed credentials", "fail:") < 0) {
        ipc_close(ch);
        return EXIT_FAILURE;
    }
    sleep(2);

    /* 4. Nonexistent user. */
    mlen = ipc_msg_build3(msg, sizeof(msg), IPC_TYPE_CRED, "nonexistent", "password", "");
    if (mlen < 0 || send_and_recv(ch, msg, (size_t)mlen, "nonexistent user", "fail:") < 0) {
        ipc_close(ch);
        return EXIT_FAILURE;
    }

    sleep(2);

    /* 5. Correct credentials. */
    mlen = ipc_msg_build3(msg, sizeof(msg), IPC_TYPE_CRED, USERNAME, PASSWORD, "");
    if (mlen < 0 || send_and_recv(ch, msg, (size_t)mlen, "correct credentials", "ok") < 0) {
        ipc_close(ch);
        return EXIT_FAILURE;
    }

    ipc_close(ch);
    fprintf(stderr, "Fake greeter exiting\n");
    return EXIT_SUCCESS;
}
