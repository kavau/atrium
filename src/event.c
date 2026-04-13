#include "event.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

/*
 * Maximum number of fds that can be registered with the event loop at once.
 * The expected set is small and static: one signalfd, one sd-bus fd, one udev
 * monitor fd (3 total), plus one credential-pipe read end per seat. A
 * generously large multiseat setup with 8 seats therefore uses 11 fds. 32
 * leaves ample headroom for future additions without being wasteful.
 */
#define MAX_FDS 32

struct fd_entry {
    event_cb  cb;
    void     *userdata;
};

static struct pollfd   g_fds[MAX_FDS];
static struct fd_entry g_entries[MAX_FDS];
static int             g_num_entries = 0;
static int             g_running = 0;

int event_add(int fd, event_cb cb, void *userdata)
{
    assert(fd >= 0);
    assert(cb != NULL);

    if (g_num_entries >= MAX_FDS) {
        fprintf(stderr, "atrium: event_add: fd limit (%d) reached, cannot register fd %d\n",
                MAX_FDS, fd);
        return -1;
    }

    g_fds[g_num_entries]     = (struct pollfd){ .fd = fd, .events = POLLIN };
    g_entries[g_num_entries] = (struct fd_entry){ .cb = cb, .userdata = userdata };
    g_num_entries++;
    return 0;
}

void event_remove(int fd)
{
    assert(fd >= 0);

    for (int i = 0; i < g_num_entries; i++) {
        if (g_fds[i].fd == fd) {
            /* Fill the hole by moving the tail entry into this slot.
             * Order is not preserved, but that's fine for our purposes. */
            g_num_entries--;
            g_fds[i]     = g_fds[g_num_entries];
            g_entries[i] = g_entries[g_num_entries];
            return;
        }
    }
}

void event_loop_run(void)
{
    g_running = 1;

    while (g_running) {
        int n = poll(g_fds, g_num_entries, -1);
        if (n < 0) {
            /* EINTR means poll() was interrupted by a signal before any fd
             * became ready. With signalfd we've already blocked all signals
             * via sigprocmask, so this shouldn't happen in normal operation,
             * but it's harmless to retry. */
            if (errno == EINTR)
                continue;
            fprintf(stderr, "atrium: poll: %s\n", strerror(errno));
            break;
        }
        /*
         * Callbacks must not call event_remove() on the fd they are currently
         * handling. Removal swaps the removed slot with the tail entry; if that
         * tail entry also had POLLIN set this round, it would be skipped until
         * the next poll() call. In practice, callbacks in atrium only remove
         * fds during teardown, never inside a read-event handler.
         */
        for (int i = 0; i < g_num_entries; i++) {
            if (g_fds[i].revents & POLLIN)
                g_entries[i].cb(g_fds[i].fd, g_entries[i].userdata);
        }
    }
}

void event_loop_quit(void)
{
    g_running = 0;
}
