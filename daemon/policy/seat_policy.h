#pragma once

/*
 * seat_policy.h - per-seat policy state, embedded in struct seat.
 *
 * Owned by daemon/policy: nothing outside policy.c may read or write these
 * fields.
 */

#include <stdint.h>
#include <time.h>

typedef struct seat_policy {
    int             crash_count;        /* Number of abnormal runner exits... */
    struct timespec crash_window_start; /* ...since start of crash window */
    int64_t backoff_until_ms; /* deadline until which to suppress DRM events after a crash */
} seat_policy;
