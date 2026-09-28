/*
 * fake-compositor.c - a fake compositor for testing the session runner
 *
 * By default, the fake compositor exits immediately on SIGTERM.
 *
 *   --exit-delay <n>   exit n seconds after SIGTERM
 *   --ignore-sigterm   do not exit on sigterm; only SIGKILL stops it
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_sigterm_count = 0;

static void on_sigterm(int sig) {
    (void)sig;
    g_sigterm_count++;
}

int main(int argc, char **argv) {
    int ignore_sigterm = 0;
    int exit_delay = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ignore-sigterm") == 0) {
            ignore_sigterm = 1;
        } else if (strcmp(argv[i], "--exit-delay") == 0 && i + 1 < argc) {
            exit_delay = atoi(argv[++i]);
        } else {
            fprintf(stderr, "fake-compositor: unknown argument '%s'\n", argv[i]);
            return 2;
        }
    }

    struct sigaction sa = {.sa_handler = on_sigterm, .sa_flags = 0};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    fprintf(stderr, "fake compositor started, PID %d (ignore_sigterm=%d exit_delay=%d)\n", getpid(),
            ignore_sigterm, exit_delay);

    int reported = 0;
    while (1) {
        pause(); /* returns on any delivered signal */
        if (g_sigterm_count == reported)
            continue;
        reported = g_sigterm_count;

        if (ignore_sigterm) {
            fprintf(stderr, "fake compositor: SIGTERM #%d ignored, still running\n", reported);
            continue;
        }
        if (exit_delay > 0) {
            fprintf(stderr, "fake compositor: SIGTERM received, exiting in %d s\n", exit_delay);
            sleep((unsigned)exit_delay);
        }
        fprintf(stderr, "fake compositor: exiting\n");
        return 0;
    }
}
