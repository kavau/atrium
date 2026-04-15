#pragma once

#include <stdint.h>
#include <sys/types.h>

/*
 * seat.h — seat list
 *
 * The seat list is a static array populated at startup by bus.c via
 * seat_add(). MAX_SEATS is the upper bound on simultaneously supported seats.
 */

#define MAX_SEATS 16

struct seat {
    char  name[64];
    int   vtnr;              /* allocated VT number; 0 for non-seat0 seats */

    /* Session state (populated by session_start). */
    pid_t compositor_pid;    /* 0 when no compositor is running */
    int   session_fifo_fd;   /* logind session fifo; close to end session; -1 when idle */
    char  session_id[64];    /* logind session id, e.g. "c1" */
    char  session_object[256]; /* logind session object path */
    char  runtime_path[256]; /* XDG_RUNTIME_DIR for the session */
};

/* Add a seat by name. Returns 0 on success, -1 if the seat limit is reached
 * (error is logged). */
int seat_add(const char *name);

/* Return the number of seats in the list. */
int seat_count(void);

/* Return a pointer to seat i (0-based). Returns NULL if i is out of range. */
struct seat *seat_get(int i);
