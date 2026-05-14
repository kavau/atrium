#include "runner.h"

#include <assert.h>
#include <unistd.h>

#include "daemon/session/session_runner.h"
#include "lib/log.h"
#include "seat.h"

int runner_start(const char *pam_conf_path, seat *s) {
    assert(s);

    pid_t runner_pid = fork();
    if (runner_pid < 0) {
        log_syserr("runner_start: fork");
        return 1;
    }

    if (runner_pid == 0) {
        /* Child process -- run the full session lifecycle. */
        log_debug("starting session runner on seat '%s'", s->name);
        session_runner(pam_conf_path, s);
        /* unreachable */
    }

    /* Parent process -- update seat state and return to the main loop. */
    s->state = SEAT_RUNNING;
    s->runner_pid = runner_pid;
    log_info("started session runner with PID %d on seat '%s'", runner_pid, s->name);
    return 0;
}
