#pragma once

#include "auth.h"
#include "config.h"

#include <stdint.h>
#include <sys/types.h>

/*
 * seat.h — seat list
 *
 * The seat list is a static array populated at startup by bus.c via
 * seat_add(). MAX_SEATS is the upper bound on simultaneously supported seats.
 */

#define MAX_SEATS 16

/* Seat states. */
#define SEAT_IDLE     0   /* no compositor or greeter running */
#define SEAT_GREETER  1   /* cage+greeter running; credentials pipe open */
#define SEAT_SESSION  2   /* compositor running */

struct seat {
    char  name[64];
    int   vtnr;              /* allocated VT number; 0 for non-seat0 seats */
    int   vt_kb_fd;          /* tty fd with keyboard suppressed; -1 if none */

    /* Session state (populated by session_start / session_start_greeter). */
    int   state;             /* SEAT_IDLE, SEAT_GREETER, or SEAT_SESSION */
    pid_t compositor_pid;    /* 0 when no compositor is running */
    int   session_fifo_fd;   /* logind session fifo; close to end session; -1 when idle */
    char  session_id[64];    /* logind session id, e.g. "c1" */
    char  session_object[256]; /* logind session object path */
    char  runtime_path[256]; /* XDG_RUNTIME_DIR for the session */

    /* Greeter IPC state (populated by greeter_start). */
    int   credentials_rfd;   /* read end of greeter→daemon credential pipe; -1 if none */
    int   result_wfd;        /* write end of daemon→greeter result pipe; -1 if none */
    char  greeter_username[CONFIG_MAX_USERNAME_LEN]; /* username received from greeter credentials */

    /* PAM session state (populated by auth_begin in on_greeter_credentials). */
    auth_result auth;        /* valid while state == SEAT_SESSION */
};

/* Add a seat by name. Returns 0 on success, -1 if the seat limit is reached
 * (error is logged). */
int seat_add(const char *name);

/* Return the number of seats in the list. */
int seat_count(void);

/* Return a pointer to seat i (0-based). Returns NULL if i is out of range. */
struct seat *seat_get(int i);
