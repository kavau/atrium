#pragma once

/*
 * seat.h - seat management
 *
 * Keeps a list of currently active seats and their state.
 */

#include "lib/defs.h"

/* Holds the current state of a seat */
typedef struct seat {
    char name[MAX_LEN_SEAT];
    int vtnr; /* allocated VT number; 0 for non-seat0 seats */
} seat;

/* Add a seat with the given name and VT number (only relevant for seat0).
Returns the address of the newly allocated seat, or NULL on failure. */
seat *seat_add(const char *name, int vtnr);

/* Seat iteration - return the first seat (or NULL if no seats exist) */
seat *seat_first(void);

/* Seat iteration - return the next seat or NULL */
seat *seat_next(const seat *s);
