#pragma once

#include <stdint.h>
#include <sys/types.h>

/*
 * bus.h — D-Bus / logind interface
 */

/* Open the system bus and register its fd with the event loop.
 * Returns 0 on success, -1 on error (error is logged). */
int bus_open(void);

/* Call ListSeats() on logind and populate the seat list.
 * Returns 0 on success, -1 on error (error is logged). */
int bus_enumerate_seats(void);

/* Call logind's CreateSession() to create a session for the given seat.
 * Returns 0 on success and fills the out parameters with the session
 * identifier, object path, runtime path, and fifo fd.  The fifo fd must be
 * kept open for the session lifetime; close() ends the session.
 * Returns -1 on error (error is logged). */
int bus_create_session(const char *seat_id, uint32_t vtnr, uid_t uid, pid_t pid,
                       char *session_id_out, size_t session_id_size,
                       char *obj_out, size_t obj_size,
                       char *runtime_out, size_t runtime_size,
                       int *fifo_fd_out);

/* Activate the specified logind session (triggers VT switch on seat0).
 * Returns 0 on success, -1 on error (error is logged). */
int bus_activate_session(const char *session_object);

/* Unregister from the event loop and close the system bus. */
void bus_close(void);
