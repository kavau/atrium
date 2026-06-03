#pragma once

/*
 * lock.h - login lock management
 *
 * If a user logs in simultaneously on multiple seats, this can lead to cross-
 * talk between sessions. Hence we disallow duplicate logins by default (this
 * can be overridden in the config).
 */

#include <sys/types.h>

typedef enum {
    LOGIN_LOCK_OK = 0,        /* lock acquired successfully */
    LOGIN_LOCK_DUPLICATE = 1, /* uid already logged in on another seat */
    LOGIN_LOCK_ERROR = 2,     /* system error */
} login_lock_status;

/* Attempt to acquire an exclusive login lock for the given uid. */
login_lock_status acquire_login_lock(uid_t uid);

/* Release the active login lock (if any). Safe to call if no lock is held. */
void release_login_lock(void);
