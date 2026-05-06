#include "seat.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "lib/log.h"

typedef struct seat_entry {
    seat s; /* s must remain first: seat* and seat_entry* are aliased */
    struct seat_entry *next;
} seat_entry;

/* Holds a singly-linked list of active seat structs. Each call to seat_add()
appends to the front of the list - order does not matter here. */
static seat_entry *g_seats = NULL;

seat *seat_add(const char *name, int vtnr) {
    assert(name);

    seat_entry *e = calloc(1, sizeof(seat_entry));
    if (!e) {
        log_syserr("seat_add(%s): calloc", name);
        return NULL;
    }

    if (strlen(name) >= sizeof(e->s.name)) {
        log_error("seat_add: seat name too long (%zu chars), skipping", strlen(name));
        free(e);
        return NULL;
    }
    strncpy(e->s.name, name, sizeof(e->s.name) - 1); /* null terminator is guaranteed by calloc */
    e->s.vtnr = vtnr;
    e->s.state = SEAT_IDLE;
    /* other fields are zero-initialized by calloc */

    /* Prepend to global list. */
    e->next = g_seats;
    g_seats = e;
    return &e->s;
}

seat *seat_first(void) { return g_seats ? &g_seats->s : NULL; }

seat *seat_next(const seat *s) {
    seat_entry *e = (seat_entry *)s;
    return e->next ? &e->next->s : NULL;
}
