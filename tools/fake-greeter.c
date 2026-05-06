/*
 * fake-greeter.c - a fake greeter for testing the session runner
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
    fprintf(stderr, "Fake greeter started with PID %d\n", getpid());
    sleep(10);
    fprintf(stderr, "Fake greeter exiting\n");
    return 0;
}