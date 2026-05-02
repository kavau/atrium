#pragma once

/*
 * seat.h - seat management
 */

#define MAX_LEN_SEAT 64

/* Holds the current state of a seat */
typedef struct seat {
    char name[MAX_LEN_SEAT];
    int vtnr; /* allocated VT number; 0 for non-seat0 seats */
} seat;
