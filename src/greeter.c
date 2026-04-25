#include "greeter.h"
#include "config_file.h"
#include "log.h"
#include "session.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int greeter_start(struct seat *s, const char *message)
{
    /*
     * Two pipes connect the daemon to the greeter:
     *
     *   cr_pipe  (credentials):  greeter writes  →  daemon reads
     *   re_pipe  (result):       daemon writes   →  greeter reads
     *
     * Both are created without O_CLOEXEC so they survive the two exec
     * boundaries: daemon → cage and cage → atrium-greeter.  cage is a
     * pass-through kiosk compositor that does not touch inherited fds.
     *
     * The fd numbers are published via environment variables before the
     * fork so the child inherits both the open fds and the numbers needed
     * to reference them.  atrium-greeter reads CREDENTIALS_FD and RESULT_FD
     * in greeter/main.c.
     */
    int cr_pipe[2]; /* credentials: greeter → daemon */
    int re_pipe[2]; /* result:      daemon  → greeter */

    if (pipe(cr_pipe) < 0) {
        log_error("greeter_start(%s): pipe(credentials): %s",
                  s->name, strerror(errno));
        return -1;
    }
    if (pipe(re_pipe) < 0) {
        log_error("greeter_start(%s): pipe(result): %s",
                  s->name, strerror(errno));
        close(cr_pipe[0]);
        close(cr_pipe[1]);
        return -1;
    }

    char cfd_str[16], rfd_str[16], blank_str[16];
    snprintf(cfd_str,   sizeof(cfd_str),   "%d", cr_pipe[1]); /* greeter writes */
    snprintf(rfd_str,   sizeof(rfd_str),   "%d", re_pipe[0]); /* greeter reads  */
    snprintf(blank_str, sizeof(blank_str), "%d", config_blank_timeout());

    setenv("CREDENTIALS_FD",      cfd_str,   1);
    setenv("RESULT_FD",           rfd_str,   1);
    setenv("ATRIUM_BLANK_TIMEOUT", blank_str, 1);
    if (message)
        setenv("ATRIUM_MESSAGE", message, 1);

    int r = session_start_greeter(s);

    /*
     * Remove the vars from the daemon's environment immediately: they are
     * only meaningful to the greeter child and must not leak into the next
     * session_start() call (which execs the compositor, not the greeter).
     */
    unsetenv("CREDENTIALS_FD");
    unsetenv("RESULT_FD");
    unsetenv("ATRIUM_BLANK_TIMEOUT");
    unsetenv("ATRIUM_MESSAGE");

    if (r < 0) {
        /* session_start_greeter already logged the failure. */
        close(cr_pipe[0]);
        close(cr_pipe[1]);
        close(re_pipe[0]);
        close(re_pipe[1]);
        return -1;
    }

    /*
     * Close the child-side ends in the parent.  Only the daemon-side ends
     * are kept: cr_pipe[0] (we read credentials from here) and re_pipe[1]
     * (we write results here).  Closing the child-side ends ensures that
     * when the greeter process group exits, a subsequent read on cr_pipe[0]
     * returns EOF rather than blocking forever.
     */
    close(cr_pipe[1]); /* greeter writes here; daemon reads [0] */
    close(re_pipe[0]); /* greeter reads here;  daemon writes [1] */

    s->credentials_rfd = cr_pipe[0];
    s->result_wfd      = re_pipe[1];
    return 0;
}

void greeter_stop(struct seat *s)
{
    if (s->credentials_rfd >= 0) {
        close(s->credentials_rfd);
        s->credentials_rfd = -1;
    }
    if (s->result_wfd >= 0) {
        close(s->result_wfd);
        s->result_wfd = -1;
    }
}

int greeter_send_result(struct seat *s, const char *msg)
{
    if (s->result_wfd < 0) {
        log_error("greeter_send_result(%s): no result fd", s->name);
        return -1;
    }
    size_t  len = strlen(msg);
    ssize_t n   = write(s->result_wfd, msg, len);
    if (n < (ssize_t)len) {
        if (n < 0)
            log_error("greeter_send_result(%s): write: %s",
                      s->name, strerror(errno));
        else
            log_error("greeter_send_result(%s): short write (%zd of %zu bytes)",
                      s->name, n, len);
        return -1;
    }
    return 0;
}
