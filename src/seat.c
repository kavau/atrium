#include "seat.h"

#include "config.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

static struct seat g_seats[MAX_SEATS];
static int         g_num_seats = 0;

int seat_add(const char *name)
{
    /* Check CONFIG_IGNORE_SEATS before adding. */
    if (CONFIG_IGNORE_SEATS && CONFIG_IGNORE_SEATS[0] != '\0' &&
        strcmp(name, CONFIG_IGNORE_SEATS) == 0) {
        log_info("seat_add: ignoring %s (CONFIG_IGNORE_SEATS)", name);
        return -1;
    }
    if (g_num_seats >= MAX_SEATS) {
        log_warn("seat_add: limit (%d) reached, ignoring %s",
                MAX_SEATS, name);
        return -1;
    }
    snprintf(g_seats[g_num_seats].name, sizeof(g_seats[g_num_seats].name),
             "%s", name);
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
