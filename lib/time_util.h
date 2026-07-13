#pragma once

/*
 * time_util.h - time helper functions
 */

#include <stdint.h>
#include <time.h>

/* Returns (end - start) in milliseconds. */
static inline long timediff_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

/* Returns the monotonic clock time in milliseconds since an arbitrary starting point. */
static inline int64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* Returns an itimerspec that fires once after the given number of milliseconds. */
static inline struct itimerspec itimerspec_ms(int ms) {
    return (struct itimerspec){.it_value = {
                                   .tv_sec = ms / 1000,
                                   .tv_nsec = (long)(ms % 1000) * 1000000L,
                               }};
}
