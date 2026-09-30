#include "ipc.h"

#include <stdio.h>
#include <string.h>

#include "lib/defs.h"
#include "lib/log.h"

ipc_status ipc_read_result(ipc_channel *ch, char *reason, size_t reason_len) {
    /* FRAGILE: assumes the reply fits in a single read(). This holds because
    the daemon writes <= PIPE_BUF bytes atomically; if messages ever exceed
    PIPE_BUF, this needs a read loop. */
    char    buf[MAX_LEN_IPC_MSG + 1]; /* +1 for the NUL ipc_recv_str() adds */
    ssize_t n = ipc_recv_str(ch, buf, sizeof(buf));
    if (n <= 0) {
        log_error("greeter: failed to receive response from daemon");
        snprintf(reason, reason_len, IPC_ERROR_INTERNAL);
        return IPC_FAIL;
    }

    log_debug("greeter: received IPC response '%s'", buf);

    if (strcmp(buf, "ok") == 0) {
        reason[0] = '\0';
        return IPC_OK;
    }

    /* Parse "fail:<reason>" */
    if (strncmp(buf, "fail:", 5) == 0) {
        snprintf(reason, reason_len, "%s", buf + 5);
    } else {
        snprintf(reason, reason_len, IPC_ERROR_INTERNAL);
    }
    return IPC_FAIL;
}

int ipc_send_credentials(ipc_channel *ch, const char *username, const char *password,
                         const char *session_id) {
    char    buf[MAX_LEN_IPC_MSG];
    ssize_t n = ipc_msg_build3(buf, sizeof(buf), IPC_TYPE_CRED, username, password, session_id);
    if (n < 0)
        return -1;
    int r = ipc_send(ch, buf, n);
    explicit_bzero(buf, sizeof(buf)); /* Wipe our copy of the password. */
    if (r < 0) {
        log_syserr("greeter: failed to send credentials");
        return -1;
    }
    return 0;
}
