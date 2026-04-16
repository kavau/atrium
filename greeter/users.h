#pragma once

/*
 * greeter/users.h — enumerate local users eligible for interactive login.
 *
 * Reads /etc/passwd via getpwent() and filters for uid >= 1000, uid < 65534,
 * with a valid login shell (not nologin or /bin/false).
 *
 * SHORTCUT: a future version could receive the user list from the daemon
 * via the IPC pipe, allowing policy-based filtering.
 */

#define MAX_USERS 32

typedef struct {
    char name[64];     /* pw_name (login name) */
    char display[128]; /* real name from GECOS, or pw_name if empty */
} greeter_user;

/* Populate users[] with eligible accounts.  Returns the count (≤ max). */
int enumerate_users(greeter_user *users, int max);
