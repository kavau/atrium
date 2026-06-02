#pragma once

#include <time.h>

/* Returns (end - start) in milliseconds. */
static inline long timediff_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec  - start.tv_sec)  * 1000
         + (end.tv_nsec - start.tv_nsec) / 1000000;
}
