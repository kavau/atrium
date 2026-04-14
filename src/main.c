#include "bus.h"
#include "event.h"
#include "seat.h"

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
        fprintf(stderr, "atrium: signalfd read: %s\n", strerror(errno));
        return;
    }

    switch (si.ssi_signo) {
    case SIGTERM:
        fprintf(stderr, "atrium: received SIGTERM, shutting down\n");
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
    fprintf(stderr, "atrium: starting\n");

    /* Block SIGTERM and SIGCHLD so they are delivered via signalfd only. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        fprintf(stderr, "atrium: sigprocmask: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Create a signalfd to receive the blocked signals as fd events. */
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0) {
        fprintf(stderr, "atrium: signalfd: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Register the signalfd with the event loop. */
    if (event_add(sfd, on_signal, NULL) < 0)
        return EXIT_FAILURE;

    /* Open the system bus and register it with the event loop. */
    if (bus_open() < 0)
        return EXIT_FAILURE;

    /* Enumerate seats via logind. */
    if (bus_enumerate_seats() < 0)
        return EXIT_FAILURE;
    for (int i = 0; i < seat_count(); i++)
        fprintf(stderr, "atrium: found seat: %s\n", seat_get(i)->name);

    /* Run until event_loop_quit() is called. */
    event_loop_run();

    /* Clean up. On early-exit error paths above, the process terminates and
     * the kernel releases all resources, so explicit cleanup is skipped. */
    bus_close();
    event_remove(sfd);
    close(sfd);

    fprintf(stderr, "atrium: stopped\n");
    return EXIT_SUCCESS;
}
