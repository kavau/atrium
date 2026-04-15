#include "seat.h"

#include "log.h"

#include <stdio.h>

static struct seat g_seats[MAX_SEATS];
static int         g_num_seats = 0;

int seat_add(const char *name)
{
    if (g_num_seats >= MAX_SEATS) {
        log_warn("seat_add: limit (%d) reached, ignoring %s",
                MAX_SEATS, name);
        return -1;
    }
    snprintf(g_seats[g_num_seats].name, sizeof(g_seats[g_num_seats].name),
             "%s", name);
    g_seats[g_num_seats].vtnr = 0;
    g_num_seats++;
    log_debug("added seat '%s' (total: %d)", name, g_num_seats);
    return 0;
}

int seat_count(void)
{
    return g_num_seats;
}

struct seat *seat_get(int i)
{
    if (i < 0 || i >= g_num_seats)
        return NULL;
    return &g_seats[i];
}
