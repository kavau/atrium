#include "greeter.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "daemon/session/session_runner.h"
#include "lib/log.h"
#include "seat.h"

int greeter_start(const char *pam_conf_path, seat *s) {
    assert(s);

    /* Fork the session runner */
    pid_t runner_pid = fork();
    if (runner_pid < 0) {
        log_syserr("greeter_start: fork");
        return 1;
    }

    if (runner_pid == 0) {
        /* This is the child process -- run the session setup and exec. */
        log_debug("starting session runner for greeter on seat '%s'", s->name);
        session_runner(GREETER_USERNAME, "", pam_conf_path, s, SESSION_GREETER);
    }

    /* This is the parent process -- update seat state and return to the main loop. */
    s->state = SEAT_GREETER;
    s->runner_pid = runner_pid;
    log_info("started session runner with PID %d for greeter on seat '%s'", s->runner_pid, s->name);
    return 0;
}
