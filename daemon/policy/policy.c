#include "policy.h"

#include "config.h"
#include "lib/log.h"
#include "lib/time_util.h"

static void reset_seat(seat *s) {
    s->crash_count = 0;
    s->crash_window_start = (struct timespec){0};
    s->backoff_until_ms = 0;
}

retry_verdict policy_should_retry_seat(seat *s, retry_reason reason, int *delay_ms) {
    if (config_is_seat_ignored(s->name)) {
        log_info("ignoring seat '%s' (listed in config)", s->name);
        return RETRY_NONE;
    }

    if (reason == REASON_EXITED) {
        /* Seat exited cleanly - reset crash history and restart the seat. */
        if (s->crash_count > 0)
            log_debug("seat '%s': exited cleanly, resetting crash history", s->name);
        reset_seat(s);
        return RETRY_NOW;
    }

    if (reason == REASON_IDLE) {
        /* Seat idle, e.g. at startup or after restart delay - simply restart the seat. */
        return RETRY_NOW;
    }

    if (reason == REASON_DRM_EVENT) {
        /* DRM event - check whether we are in backoff mode. We deliberately do
        not reset the crash history here, so that a spurious event does not
        trigger an entire batch of attempts. */
        if (s->backoff_until_ms && mono_ms() < s->backoff_until_ms) {
            log_debug("seat '%s': DRM event suppressed (backoff active)", s->name);
            return RETRY_NONE; /* Suppress the event. */
        }
        return RETRY_NOW;
    }

    /* Seat crashed - suppress DRM change events for a short time period. A
    crashing greeter emits such events, which would otherwise trigger an
    immediate restart. */
    s->backoff_until_ms = mono_ms() + config_drm_backoff();

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
        log_error("seat '%s': %d crashes in %ld ms, giving up", s->name, s->crash_count,
                  elapsed_ms);
        return RETRY_NONE;
    }

    if (delay_ms)
        *delay_ms = config_crash_restart_delay();
    log_info("seat '%s': crash %d/%d, restarting after %d ms", s->name, s->crash_count,
             config_crash_count_limit(), delay_ms ? *delay_ms : 0);
    return RETRY_DELAYED;
}

void policy_reset(void) {
    for (seat *s = seat_first(); s; s = seat_next(s))
        reset_seat(s);
}
