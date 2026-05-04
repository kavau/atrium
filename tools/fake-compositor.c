/*
 * fake-compositor.c - a fake compositor for testing the session runner
 *
 * Currently it just sleeps for a while then exits.
 * 
 * Usage: in lib/defs.h,
 * #define HEADLESS 1
 * #define COMPOSITOR "/usr/local/bin/atrium-fake-compositor"
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    fprintf(stderr, "Fake compositor started with PID %d\n", getpid());
    sleep(10);
    fprintf(stderr, "Fake compositor exiting\n");
    return 0;
}