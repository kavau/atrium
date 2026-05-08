#include "session.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "daemon/session/session_runner.h"
#include "lib/log.h"
#include "seat.h"

int session_start(const char *username, const char *password, const char *pam_conf_path, seat *s) {
    assert(username);
    assert(password);
    assert(s);

    /* Fork the session runner */
    pid_t runner_pid = fork();
    if (runner_pid < 0) {
        log_syserr("session_start: fork");
        return 1;
    }

    if (runner_pid == 0) {
        /* This is the child process -- run the session setup and exec. */
        log_debug("starting session runner for seat '%s'", s->name);
        session_runner(username, password, pam_conf_path, s, SESSION_USER, NULL);
    }

    /* This is the parent process -- update seat state and return to the main loop. */
    s->state = SEAT_SESSION;
    s->runner_pid = runner_pid;
    log_info("started session runner with PID %d on seat '%s'", s->runner_pid, s->name);
    return 0;
}
