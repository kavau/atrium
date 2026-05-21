#include "ipc.h"

#include "lib/log.h"

ipc_status ipc_read_result(ipc_channel *ch) {
    /* FRAGILE: assumes the reply fits in a single read(). This holds because
    the daemon writes <= PIPE_BUF bytes atomically; if messages ever exceed
    PIPE_BUF, this needs a read loop. */
    char    buf[512];
    ssize_t n = ipc_recv(ch, buf, sizeof(buf) - 1);
    if (n <= 0) {
        log_error("greeter: failed to receive response from daemon");
        return IPC_FAIL;
    }

    buf[n] = '\0';
    log_debug("greeter: received IPC response '%s'", buf);
    return strcmp(buf, "ok\n") == 0 ? IPC_OK : IPC_FAIL;
}

/* Construct credentials string for daemon: "<username>\0<password>\0" */
static int build_credentials_str(char *buf, size_t buflen, const char *username,
                                 const char *password) {
    size_t ulen = strlen(username) + 1; /* include the \0 */
    size_t plen = strlen(password) + 1;
    if (ulen + plen > buflen) {
        log_error("greeter: credentials too long to send");
        return -1;
    }
    memcpy(buf, username, ulen);
    memcpy(buf + ulen, password, plen);
    return ulen + plen;
}

int ipc_send_credentials(ipc_channel *ch, const char *username, const char *password) {
    char buf[512];
    int  n = build_credentials_str(buf, sizeof(buf), username, password);
    if (n < 0)
        return -1;
    if (ipc_send(ch, buf, n) < 0) {
        log_syserr("greeter: failed to send credentials");
        return -1;
    }
    return 0;
}