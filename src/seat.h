#pragma once

/*
 * seat.h — seat list
 *
 * The seat list is a static array populated at startup by bus.c via
 * seat_add(). MAX_SEATS is the upper bound on simultaneously supported seats.
 */

#define MAX_SEATS 16

struct seat {
    char name[64];
    int  vtnr;   /* allocated VT number; 0 for non-seat0 seats */
    int  vt_fd;  /* open fd for /dev/ttyN; -1 for non-seat0 seats */
};

/* Add a seat by name. Returns 0 on success, -1 if the seat limit is reached
 * (error is logged). */
int seat_add(const char *name);

/* Return the number of seats in the list. */
int seat_count(void);

/* Return a pointer to seat i (0-based). Returns NULL if i is out of range. */
struct seat *seat_get(int i);
