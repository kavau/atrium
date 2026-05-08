#include "greeter.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "daemon/session/session_runner.h"
#include "lib/ipc.h"
#include "lib/log.h"
#include "seat.h"

int greeter_start(const char *pam_conf_path, seat *s) {
    assert(s);

    /* Create an IPC channel for daemon-greeter communication. */
    ipc_channel *parent_end, *child_end;
    if (ipc_create(&parent_end, &child_end) < 0) {
        log_syserr("greeter_start: ipc_create");
        return 1;
    }

    /* Fork the session runner */
    pid_t runner_pid = fork();
    if (runner_pid < 0) {
        log_syserr("greeter_start: fork");
        ipc_close(parent_end);
        ipc_close(child_end);
        return 1;
    }

    if (runner_pid == 0) {
        /* This is the child process -- run the session setup and exec. */
        log_debug("starting session runner for greeter on seat '%s'", s->name);
        ipc_close(parent_end);
        session_runner(GREETER_USERNAME, "", pam_conf_path, s, SESSION_GREETER, child_end);
    }

    /* This is the parent process -- update seat state and return to the main loop. */
    ipc_close(child_end);

    s->state = SEAT_GREETER;
    s->runner_pid = runner_pid;
    s->greeter_ipc = parent_end;

    log_info("started session runner with PID %d for greeter on seat '%s'", s->runner_pid, s->name);
    return 0;
}
