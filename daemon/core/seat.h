#pragma once

/*
 * seat.h - seat management
 */

#include "lib/defs.h"

/* Holds the current state of a seat */
typedef struct seat {
    char name[MAX_LEN_SEAT];
    int vtnr; /* allocated VT number; 0 for non-seat0 seats */
} seat;
