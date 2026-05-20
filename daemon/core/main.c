#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
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

    /* Block SIGTERM and SIGCHLD immediately so they are delivered via signalfd
    rather than asynchronously. Must happen before any sleep or fork. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        log_syserr("main: sigprocmask");
        return EXIT_FAILURE;
    }

    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd < 0) {
        log_syserr("main: signalfd");
        return EXIT_FAILURE;
    }

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
        if (!seat_add(seat_names[i], strcmp(seat_names[i], "seat0") ? 0 : vtnr))
            log_error("failed to add seat '%s'", seat_names[i]);
    }

    /* Start a session runner on each seat. */
    for (seat *s = seat_first(); s; s = seat_next(s)) {
        log_info("starting session runner on seat '%s'", s->name);
        if (runner_start(PAM_CONF_PATH, s) != 0)
            log_error("failed to launch runner on seat '%s'", s->name); /* TODO: retry */
        sleep(5); /* SHORTCUT: wait a bit before starting the next runner */
    }

    /* Event loop: wait for SIGCHLD (child exited) or SIGTERM (shutdown). */
    struct pollfd pfd = {.fd = sfd, .events = POLLIN};
    while (1) {
        if (poll(&pfd, 1, -1) < 0) {
            if (errno == EINTR)
                continue;
            log_syserr("main: poll");
            break;
        }

        struct signalfd_siginfo si;
        if (read(sfd, &si, sizeof(si)) != (ssize_t)sizeof(si)) {
            log_syserr("main: read signalfd");
            break;
        }

        if ((int)si.ssi_signo == SIGCHLD) {
            /* Drain all ready children -- SIGCHLD coalesces so one signal may
            cover multiple exits. */
            while (1) {
                int wstatus;
                pid_t pid = waitpid(-1, &wstatus, WNOHANG);
                if (pid <= 0)
                    break;

                for (seat *s = seat_first(); s; s = seat_next(s)) {
                    if (pid != s->runner_pid)
                        continue;

                    if (WIFEXITED(wstatus))
                        log_debug("session runner (PID %d) on seat '%s' exited with status %d", pid,
                                  s->name, WEXITSTATUS(wstatus));
                    else if (WIFSIGNALED(wstatus))
                        log_warn(
                            "session runner (PID %d) on seat '%s' terminated by signal %d (%s)",
                            pid, s->name, WTERMSIG(wstatus), strsignal(WTERMSIG(wstatus)));
                    else
                        log_warn(
                            "session runner (PID %d) on seat '%s' exited with unexpected status %d",
                            pid, s->name, wstatus);

                    s->runner_pid = 0;
                    s->state = SEAT_IDLE;

                    log_info("restarting session runner on seat '%s'", s->name);
                    sleep(1); /* SHORTCUT: avoid tight crash-loop */
                    if (runner_start(PAM_CONF_PATH, s) != 0)
                        log_error("failed to restart runner on seat '%s'", s->name);
                    break;
                }
            }
        } else if ((int)si.ssi_signo == SIGTERM) {
            log_info("received SIGTERM, shutting down");
            break;
        }
    }

    close(sfd);

    /* Shutdown: stop all session runners before releasing the VT. */
    for (seat *s = seat_first(); s; s = seat_next(s))
        runner_stop(s);

    if (vtnr > 0) {
        vt_restore_keyboard(vtnr);
        vt_release(vtnr);
    }
    return EXIT_SUCCESS;
}
