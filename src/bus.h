#pragma once

/*
 * bus.h — D-Bus / logind interface
 */

/* Open the system bus and register its fd with the event loop.
 * Returns 0 on success, -1 on error (error is logged). */
int bus_open(void);

/* Call ListSeats() on logind and populate the seat list.
 * Returns 0 on success, -1 on error (error is logged). */
int bus_enumerate_seats(void);

/* Unregister from the event loop and close the system bus. */
void bus_close(void);
