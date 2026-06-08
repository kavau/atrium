#pragma once

/*
 * seat.h - seat management
 *
 * Keeps a list of currently active seats and their state.
 */

#include <sys/types.h>
#include <time.h>

#include "lib/defs.h"

typedef enum {
    SEAT_IDLE = 0, /* no session runner active */
    SEAT_RUNNING,  /* session runner active */
} seat_state;

/* Holds the current state of a seat */
typedef struct seat {
    char name[MAX_LEN_SEAT];
    int vtnr;         /* allocated VT number; 0 for non-seat0 seats */
    seat_state state; /* current state: SEAT_IDLE or SEAT_RUNNING */
    pid_t runner_pid; /* PID of the session runner, or 0 if none */
    /* Crash-loop detection */
    int             crash_count;        /* Number of abnormal runner exits... */
    struct timespec crash_window_start; /* ...since start of crash window */
} seat;

/* Add a seat with the given name and VT number (only relevant for seat0).
Returns the address of the newly allocated seat, or NULL on failure. */
seat *seat_add(const char *name, int vtnr);

/* Seat iteration - return the first seat (or NULL if no seats exist) */
seat *seat_first(void);

/* Seat iteration - return the next seat or NULL */
seat *seat_next(const seat *s);

/* Return the seat whose runner_pid matches pid, or NULL if not found. */
seat *seat_find_by_pid(pid_t pid);

/* Return the seat with the given name, or NULL if not found. */
seat *seat_find_by_name(const char *name);
