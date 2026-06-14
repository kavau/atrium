#include "runner.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "daemon/session/session_runner.h"
#include "lib/log.h"
#include "lib/proc.h"
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

        /* Restore the default signal mask so runner_stop() reaches us and
        children inherit a clean mask. */
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, NULL);

        /* Rename ourselves to "atrium-<seat_name>" to distinguish from daemon.
        Truncation to 15 chars is intentional. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        char proc_name[16];
        snprintf(proc_name, sizeof(proc_name), "atrium-%s", s->name);
#pragma GCC diagnostic pop
        prctl(PR_SET_NAME, proc_name);

        session_runner(pam_conf_path, s);
        /* unreachable */
    }

    /* Parent process -- update seat state and return to the main loop. */
    s->state = SEAT_RUNNING;
    s->runner_pid = runner_pid;
    log_info("started session runner with PID %d on seat '%s'", runner_pid, s->name);
    return 0;
}

void runner_stop(seat *s) {
    if (s->runner_pid <= 0)
        return;

    log_info("stopping session runner (PID %d) on seat '%s'", s->runner_pid, s->name);
    kill_and_wait(s->runner_pid, "session runner", s->name);
    s->runner_pid = 0;
    s->state = SEAT_IDLE;
}
