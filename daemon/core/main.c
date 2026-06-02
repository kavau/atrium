#include <assert.h>
#include <errno.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bus.h"
#include "config.h"
#include "lib/defs.h"
#include "lib/log.h"
#include "lib/time_util.h"
#include "runner.h"
#include "seat.h"
#include "vt.h"

/* Records an abnormal runner exit for s and returns 1 if the seat should be
retried, or 0 if the crash limit has been reached. */
static int seat_should_retry(seat *s) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = 0;

    if (s->crash_count++ > 0) {
        /* Fixed time window anchored at the first crash - not as accurate as a
        sliding window, but simpler and good enough for our purpose. */
        elapsed_ms = timediff_ms(s->crash_window_start, now);
        if (elapsed_ms > (long)config_crash_window() * 1000) {
            s->crash_count = 1; /* Window expired - start a new window. */
        }
    }
    if (s->crash_count <= 1) {
        s->crash_window_start = now; /* First crash - start a new window. */
    }
    if (s->crash_count >= config_crash_count_limit()) {
        log_error("seat '%s': %d crashes in %ld ms, giving up", s->name,
                  s->crash_count, elapsed_ms);
        return 0;
    }
    return 1;
}

static void on_seat_discovered(const char *seat_id, void *userdata) {
    if (config_is_seat_ignored(seat_id)) {
        log_info("ignoring seat '%s' (listed in config)", seat_id);
        return;
    }

    int seat0_vtnr = *(int *)userdata;
    /* Only seat0 is associated with a VT. */
    int vtnr = strcmp(seat_id, "seat0") == 0 ? seat0_vtnr : 0;
    if (vtnr > 0)
        log_info("discovered seat '%s' (vtnr=%d)", seat_id, vtnr);
    else
        log_info("discovered seat '%s'", seat_id);
    if (!seat_add(seat_id, vtnr))
        log_error("on_seat_discovered: seat_add failed for '%s'", seat_id);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    log_info("starting");
    log_debug("debug logging enabled");
    config_load("/etc/atrium.conf");

    /* Ignore SIGPIPE globally so a broken IPC pipe returns EPIPE from write()
    rather than killing the process. Inherited by all child processes. */
    signal(SIGPIPE, SIG_IGN);

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

    if (bus_open() < 0)
        return EXIT_FAILURE;

    /* SHORTCUT: allow hardware initialization to complete before seat discovery */
    if (config_seat_discovery_delay() > 0)
        usleep((useconds_t)config_seat_discovery_delay() * 1000);

    /* Allocate a VT for seat0. SHORTCUT: needs to be done after seat0 is discovered. */
#if HEADLESS
    log_info("headless mode enabled, skipping VT allocation");
    int vtnr = 0;
#else
    int vtnr = vt_alloc();
    if (vtnr < 0) {
        log_error("failed to allocate VT for seat0: %d", vtnr);
        bus_close();
        return EXIT_FAILURE;
    }
    int vt_kb_mode = K_UNICODE; /* saved keyboard mode; restored at shutdown */
    vt_suppress_keyboard(vtnr, &vt_kb_mode);
#endif

    if (bus_enumerate_seats(on_seat_discovered, &vtnr) < 0) {
        log_error("failed to enumerate seats");
        bus_close();
        return EXIT_FAILURE;
    }

    /* Start a session runner on each seat. */
    for (seat *s = seat_first(); s; s = seat_next(s)) {
        log_info("starting session runner on seat '%s'", s->name);
        if (runner_start(PAM_CONF_PATH, s) != 0)
            log_error("failed to launch runner on seat '%s'", s->name); /* TODO: retry */
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
            /* Drain all ready children */
            int   wstatus;
            pid_t pid;
            while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
                seat *s = seat_find_by_pid(pid);
                if (!s) {
                    log_debug("reaped unknown PID %d", (int)pid);
                    continue;
                }

                if (WIFEXITED(wstatus))
                    log_debug("session runner (PID %d) on seat '%s' exited with status %d", pid,
                              s->name, WEXITSTATUS(wstatus));
                else if (WIFSIGNALED(wstatus))
                    log_warn("session runner (PID %d) on seat '%s' terminated by signal %d (%s)",
                             pid, s->name, WTERMSIG(wstatus), strsignal(WTERMSIG(wstatus)));
                else
                    log_warn(
                        "session runner (PID %d) on seat '%s' exited with unexpected status %d",
                        pid, s->name, wstatus);

                s->runner_pid = 0;
                s->state = SEAT_IDLE;

                /* Crash-loop detection */
                if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) { /* Normal exit */
                    log_info("restarting session runner on seat '%s'", s->name);
                } else if (seat_should_retry(s)) {
                    log_info("seat '%s': crash %d/%d, restarting after %d ms",
                             s->name, s->crash_count, config_crash_count_limit(),
                             config_crash_restart_delay());
                } else {
                    continue; /* crash limit reached - leave seat idle */
                }
                usleep((useconds_t)config_crash_restart_delay() * 1000);
                if (runner_start(PAM_CONF_PATH, s) != 0)
                    log_error("failed to restart runner on seat '%s'", s->name);
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
        vt_restore_keyboard(vtnr, vt_kb_mode);
        vt_release(vtnr);
    }
    bus_close();
    return EXIT_SUCCESS;
}
