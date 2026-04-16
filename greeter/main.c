/*
 * greeter/main.c — atrium greeter entry point.
 *
 * The daemon forks and execs this binary inside a cage compositor session,
 * with ATRIUM_USERNAME set to the account name for this seat.  This file
 * reads that variable and hands off to greeter_run_ui(), which blocks until
 * the user dismisses the window.  Exiting 0 signals the daemon that the
 * greeter completed successfully and the session should start.
 *
 * The UI implementation lives in ui-gtk4.c.  Swapping toolkits means
 * replacing that file only — this file and the daemon are unaffected.
 */

#include "ui.h"
#include "log.h"

#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    log_info("starting (pid=%d)", (int)getpid());
    /*
     * ATRIUM_USERNAME is optional: when set by the daemon it pre-fills the
     * username entry; when absent the field starts empty.
     *
     * CREDENTIALS_FD and RESULT_FD carry the fd numbers of the two IPC pipes
     * set up by the daemon (or greeter-test) before exec.  When absent
     * (standalone dev run) both default to -1 and the greeter falls back to
     * quitting directly on button press.
     */
    const char *username = getenv("ATRIUM_USERNAME");
    if (!username)
        username = "";

    int credentials_fd = -1, result_fd = -1;
    const char *cfd = getenv("CREDENTIALS_FD");
    const char *rfd = getenv("RESULT_FD");
    if (cfd && *cfd) credentials_fd = atoi(cfd);
    if (rfd && *rfd) result_fd      = atoi(rfd);

    log_debug("username='%s' credentials_fd=%d result_fd=%d",
             username, credentials_fd, result_fd);

    greeter_run_ui(username, credentials_fd, result_fd);
    log_info("exiting");
    return EXIT_SUCCESS;
}
