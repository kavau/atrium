#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/defs.h"
#include "lib/log.h"
#include "runner.h"
#include "seat.h"
#include "vt.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    log_info("starting");
    log_debug("debug logging enabled");

    /* SHORTCUT: allow hardware initialization to complete before seat discovery */
    sleep(SEAT_DISCOVERY_DELAY);

    /* Allocate a VT for seat0. SHORTCUT: needs to be done after seat0 is discovered. */
#if HEADLESS
    log_info("headless mode enabled, skipping VT allocation");
    int vtnr = 0;
#else
    int vtnr = vt_alloc();
    if (vtnr < 0) {
        log_error("failed to allocate VT for seat0: %d", vtnr);
        return EXIT_FAILURE;
    }
    vt_suppress_keyboard(vtnr);
#endif

    /* SHORTCUT: create hardcoded seats. We add seats in reverse order because
    seat_add appends to the front (although order does not matter) */
    char *seat_names[] = {SEATS};
    int n_seats = sizeof(seat_names) / sizeof(seat_names[0]);
    for (int i = n_seats - 1; i >= 0; i--) {
        log_info("adding seat '%s'", seat_names[i]);
        if (!seat_add(seat_names[i], strcmp(seat_names[i], "seat0") ? 0 : vtnr)) {
            log_error("failed to add seat '%s'", seat_names[i]);
        }
    }

    /* Start a session runner on each seat. */
    for (seat *s = seat_first(); s; s = seat_next(s)) {
        log_info("starting session runner on seat '%s'", s->name);
        int r = runner_start(PAM_CONF_PATH, s);
        if (r != 0) {
            log_error("failed to launch runner on %s: %d", s->name, r);
            /* TODO: retry */
        }
        sleep(5); /* SHORTCUT: wait a bit before starting the next runner */
    }

    /* Simple event loop */
    while (1) {
        int wstatus;
        pid_t pid = waitpid(-1, &wstatus, 0);
        if (pid < 0) {
            if (errno == EINTR)
                continue;
            log_syserr("waitpid");
            break; /* no children remain */
        }

        for (seat *s = seat_first(); s; s = seat_next(s)) {
            if (pid != s->runner_pid)
                continue;

            if (WIFEXITED(wstatus))
                log_debug("session runner (PID %d) on seat '%s' exited with status %d", pid,
                          s->name, WEXITSTATUS(wstatus));
            else if (WIFSIGNALED(wstatus))
                log_warn("session runner (PID %d) on seat '%s' terminated by signal %d (%s)", pid,
                         s->name, WTERMSIG(wstatus), strsignal(WTERMSIG(wstatus)));
            else
                log_warn("session runner (PID %d) on seat '%s' exited with unexpected status %d",
                         pid, s->name, wstatus);

            s->runner_pid = 0;
            s->state = SEAT_IDLE;

            log_info("restarting session runner on seat '%s'", s->name);
            sleep(1); /* SHORTCUT: avoid tight crash-loop */
            if (runner_start(PAM_CONF_PATH, s) != 0)
                log_error("failed to restart runner on seat '%s'", s->name);
            break; /* TODO: retry */
        }
    }

    /* Shutdown: stop all session runners before releasing the VT. */
    for (seat *s = seat_first(); s; s = seat_next(s))
        runner_stop(s);

    if (vtnr > 0) {
        vt_restore_keyboard(vtnr);
        vt_release(vtnr);
    }
    return EXIT_SUCCESS;
}
