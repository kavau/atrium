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

#include "lib/ipc.h"
#include "lib/log.h"

int main(void) {
    char username[LINE_MAX];
    printf("Username: ");
    if (!fgets(username, sizeof(username), stdin)) {
        log_error("failed to read username");
        return 1;
    }
    username[strcspn(username, "\n")] = '\0'; /* remove trailing newline */

    const char *password = getpass("Password: ");
    if (!password) {
        log_error("failed to read password");
        return 1;
    }

    printf("Press Enter to log in...\n");
    getchar();

    /* Send credentials to the daemon */
    ipc_channel *ch = NULL;
    if (ipc_create_from_env(&ch) < 0) {
        log_error("failed to create IPC channel");
        return EXIT_FAILURE;
    }
    char message[256];
    size_t ulen = strlen(username) + 1; /* include the \0 */
    size_t plen = strlen(password) + 1;
    if (ulen + plen > sizeof(message)) {
        log_error("credentials too long");
        ipc_close(ch);
        return EXIT_FAILURE;
    }
    memcpy(message, username, ulen);
    memcpy(message + ulen, password, plen);

    if (ipc_send(ch, message, ulen + plen) < 0) {
        log_error("failed to send message over IPC channel");
        ipc_close(ch);
        return EXIT_FAILURE;
    }
    ipc_close(ch);

    /* SHORTCUT: wait for daemon to respond */
    return EXIT_SUCCESS;
}
