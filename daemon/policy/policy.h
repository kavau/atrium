#pragma once

#include "daemon/core/seat.h"

/*
 * policy.h - policy management
 *
 * This module encapsulates all policy decisions: Policy decides and returns a
 * verdict; core executes the verdict. The intention is to simplify the rest of
 * the daemon code, which is now mostly mechanism.
 */

typedef enum {
    REASON_IDLE,     /* Seat never ran, or is waiting for a scheduled restart */
    REASON_EXITED,   /* Seat exited cleanly */
    REASON_CRASHED,  /* Seat crashed (greeter or compositor failed) */
    REASON_DRM_EVENT /* DRM event occurred (e.g. display connected/disconnected) */
} retry_reason;

typedef enum {
    RETRY_NOW,     /* Restart seat immediately */
    RETRY_DELAYED, /* Restart seat after a delay */
    RETRY_NONE     /* Do not restart seat (ignore this event, or leave idle permanently) */
} retry_verdict;

/* Decide whether to restart a session runner, based on reason and crash
history. Updates the crash count when applicable. */
retry_verdict policy_should_retry_seat(seat *s, retry_reason reason, int *delay_ms);

/* Reset the policy state (i.e. crash history) for all seats. */
void policy_reset(void);
