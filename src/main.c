#include "bus.h"
#include "event.h"
#include "log.h"
#include "seat.h"
#include "vt.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>

/* Event loop callback for the signalfd. Reads one signal and dispatches on
 * signal number. Handles SIGTERM (initiates shutdown) and SIGCHLD (deferred
 * to a later phase). */
static void on_signal(int fd, void *userdata)
{
    (void)userdata;

    struct signalfd_siginfo si;
    ssize_t n = read(fd, &si, sizeof(si));
    if (n != sizeof(si)) {
        log_error("signalfd read: %s", strerror(errno));
        return;
    }

    switch (si.ssi_signo) {
    case SIGTERM:
        log_info("received SIGTERM, shutting down");
        event_loop_quit();
        break;
    case SIGCHLD:
        /* Handled in a later phase. */
        break;
    default:
        assert(0 && "unexpected signal");
    }
}

int main(void)
{
    log_info("starting");
    log_debug("debug logging is enabled");

    /* Block SIGTERM and SIGCHLD so they are delivered via signalfd only. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        log_error("sigprocmask: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Create a signalfd to receive the blocked signals as fd events. */
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0) {
        log_error("signalfd: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Register the signalfd with the event loop. */
    if (event_add(sfd, on_signal, NULL) < 0)
        return EXIT_FAILURE;
    log_debug("registered signalfd %d", sfd);

    /* Open the system bus and register it with the event loop. */
    if (bus_open() < 0)
        return EXIT_FAILURE;
    log_debug("bus connection established");

    /* Enumerate seats via logind. */
    log_debug("discovering seats...");
    if (bus_enumerate_seats() < 0)
        return EXIT_FAILURE;
    for (int i = 0; i < seat_count(); i++)
        log_info("found seat: %s", seat_get(i)->name);

    /* Allocate a VT for seat0. */
    log_debug("allocating VT for seat0...");
    for (int i = 0; i < seat_count(); i++) {
        struct seat *s = seat_get(i);
        if (strcmp(s->name, "seat0") != 0)
            continue;
        if (vt_alloc(&s->vtnr) < 0)
            return EXIT_FAILURE;
        log_info("seat0: allocated vt%d", s->vtnr);
        break;
    }

    /* Run until event_loop_quit() is called. */
    log_debug("entering main event loop");
    event_loop_run();

    /* Clean up. On early-exit error paths above, the process terminates and
     * the kernel releases all resources, so explicit cleanup is skipped. */
    log_debug("beginning cleanup sequence");
    /* Release VT allocations. */
    int vt_count = 0;
    for (int i = 0; i < seat_count(); i++) {
        struct seat *s = seat_get(i);
        if (s->vtnr > 0) {
            vt_release(s->vtnr);
            vt_count++;
        }
    }
    if (vt_count > 0)
        log_debug("released %d VT allocations", vt_count);

    bus_close();
    event_remove(sfd);
    close(sfd);

    log_info("stopped");
    return EXIT_SUCCESS;
}
