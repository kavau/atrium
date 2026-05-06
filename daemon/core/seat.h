#pragma once

/*
 * seat.h - seat management
 *
 * Keeps a list of currently active seats and their state.
 */

#include <sys/types.h>

#include "lib/defs.h"

typedef enum {
    SEAT_IDLE = 0, /* no compositor or greeter running */
    SEAT_GREETER,  /* greeter running; credentials pipe open */
    SEAT_SESSION,  /* compositor running */
} seat_state;

/* Holds the current state of a seat */
typedef struct seat {
    char name[MAX_LEN_SEAT];
    int vtnr;         /* allocated VT number; 0 for non-seat0 seats */
    seat_state state; /* current state: SEAT_IDLE, SEAT_GREETER, or SEAT_SESSION */
    pid_t runner_pid; /* pid of the session runner or 0 if no active session */
} seat;

/* Add a seat with the given name and VT number (only relevant for seat0).
Returns the address of the newly allocated seat, or NULL on failure. */
seat *seat_add(const char *name, int vtnr);

/* Seat iteration - return the first seat (or NULL if no seats exist) */
seat *seat_first(void);

/* Seat iteration - return the next seat or NULL */
seat *seat_next(const seat *s);
