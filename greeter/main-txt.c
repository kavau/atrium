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

#include "lib/defs.h"
#include "lib/ipc.h"
#include "lib/log.h"

int main(void) {
    ipc_channel *ch = NULL;
    if (ipc_create_from_env(&ch) < 0) {
        log_error("failed to create IPC channel");
        return EXIT_FAILURE;
    }

    while (1) {
        char username[LINE_MAX];
        printf("Username: ");
        if (!fgets(username, sizeof(username), stdin)) {
            log_error("failed to read username");
            ipc_close(ch);
            return EXIT_FAILURE;
        }
        username[strcspn(username, "\n")] = '\0';

        const char *password = getpass("Password: ");
        if (!password) {
            log_error("failed to read password");
            ipc_close(ch);
            return EXIT_FAILURE;
        }

        char    message[256];
        ssize_t mlen =
            ipc_msg_build3(message, sizeof(message), IPC_TYPE_CRED, username, password, "");
        if (mlen < 0) {
            ipc_close(ch);
            return EXIT_FAILURE;
        }

        int r = ipc_send(ch, message, mlen);
        explicit_bzero(message, sizeof(message)); /* Wipe both password copies. */
        explicit_bzero((char *)password, strlen(password));
        if (r < 0) {
            log_error("failed to send credentials");
            ipc_close(ch);
            return EXIT_FAILURE;
        }

        char    result[64] = {0};
        ssize_t n = ipc_recv(ch, result, sizeof(result) - 1);
        if (n <= 0) {
            log_error("failed to read auth result");
            ipc_close(ch);
            return EXIT_FAILURE;
        }

        if (strncmp(result, "ok", 2) == 0)
            break;

        /* "fail:<reason>" - show reason and retry */
        const char *reason = strchr(result, ':');
        printf("Login failed: %s\n", reason ? reason + 1 : "unknown error");
    }

    ipc_close(ch);
    return EXIT_SUCCESS;
}
