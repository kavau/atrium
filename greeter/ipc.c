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

    if (strcmp(buf, "ok\n") == 0) {
        reason[0] = '\0';
        return IPC_OK;
    }

    /* Parse "fail:<reason>\n" */
    if (strncmp(buf, "fail:", 5) == 0) {
        const char *p = buf + 5;
        size_t      len = strlen(p);
        if (len > 0 && p[len - 1] == '\n')
            len--;
        snprintf(reason, reason_len, "%.*s", (int)len, p);
    } else {
        snprintf(reason, reason_len, IPC_ERROR_INTERNAL);
    }
    return IPC_FAIL;
}

/* Construct credentials string for daemon: "<username>\0<password>\0<session_id>\0" */
static int build_credentials_str(char *buf, size_t buflen, const char *username,
                                 const char *password, const char *session_id) {
    size_t ulen = strlen(username)   + 1; /* include the \0 */
    size_t plen = strlen(password)   + 1;
    size_t slen = strlen(session_id) + 1;
    if (ulen + plen + slen > buflen) {
        log_error("greeter: credentials too long to send");
        return -1;
    }
    memcpy(buf,               username,   ulen);
    memcpy(buf + ulen,        password,   plen);
    memcpy(buf + ulen + plen, session_id, slen);
    return (int)(ulen + plen + slen);
}

int ipc_send_credentials(ipc_channel *ch, const char *username, const char *password,
                         const char *session_id) {
    char buf[MAX_LEN_IPC_MSG];
    int  n = build_credentials_str(buf, sizeof(buf), username, password, session_id);
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
