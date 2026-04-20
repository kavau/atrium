#include "seat.h"

#include "log.h"

#include <stdio.h>
#include <string.h>

static struct seat g_seats[MAX_SEATS];
static int         g_num_seats = 0;

int seat_add(const char *name, const char *object_path)
{
    if (g_num_seats >= MAX_SEATS) {
        log_warn("seat_add: limit (%d) reached, ignoring %s",
                MAX_SEATS, name);
        return -1;
    }
    snprintf(g_seats[g_num_seats].name, sizeof(g_seats[g_num_seats].name),
             "%s", name);
    snprintf(g_seats[g_num_seats].object_path, sizeof(g_seats[g_num_seats].object_path),
             "%s", object_path ? object_path : "");
    g_seats[g_num_seats].vtnr             = 0;
    g_seats[g_num_seats].vt_kb_fd         = -1;
    g_seats[g_num_seats].state            = SEAT_IDLE;
    g_seats[g_num_seats].compositor_pid   = 0;
    g_seats[g_num_seats].session_fifo_fd  = -1;
    g_seats[g_num_seats].session_id[0]    = '\0';
    g_seats[g_num_seats].session_object[0] = '\0';
    g_seats[g_num_seats].runtime_path[0]  = '\0';
    g_seats[g_num_seats].credentials_rfd  = -1;
    g_seats[g_num_seats].result_wfd       = -1;
    g_seats[g_num_seats].greeter_username[0] = '\0';
    g_num_seats++;
    log_debug("added seat '%s' (total: %d)", name, g_num_seats);
    return 0;
}

void seat_remove(const char *name)
{
    for (int i = 0; i < g_num_seats; i++) {
        if (strcmp(g_seats[i].name, name) != 0)
            continue;
        /* SHORTCUT: shift remaining entries left to fill the gap.  This
         * invalidates any struct seat * pointers into g_seats[i+1..] that
         * were cached elsewhere (e.g. as event_add userdata for a later
         * seat's credentials fd).  Removing any seat other than the last
         * while a higher-indexed seat has an active greeter will cause a
         * use-after-move bug.  Fix: convert seat list to a linked list so
         * pointers are stable.  See Future TODOs in doc/current-work.md. */
        for (int j = i; j < g_num_seats - 1; j++)
            g_seats[j] = g_seats[j + 1];
        g_num_seats--;
        log_debug("removed seat '%s' (total: %d)", name, g_num_seats);
        return;
    }
    log_warn("seat_remove: seat '%s' not found", name);
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

struct seat *seat_find(const char *name)
{
    for (int i = 0; i < g_num_seats; i++) {
        if (strcmp(g_seats[i].name, name) == 0)
            return &g_seats[i];
    }
    return NULL;
}
