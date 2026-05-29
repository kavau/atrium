#pragma once

/*
 * bus.h - D-Bus / logind interface
 */

#include <stdint.h>
#include <sys/types.h>

/* Open a connection to the system bus. Must be called before any other bus_*
 * function. Returns 0 on success, -1 on error (error is logged). */
int bus_open(void);

/* Close the system bus connection. */
void bus_close(void);

/* Call logind's CreateSession() to create a session for the given seat. pid is
 * the session leader process (typically the compositor child). Returns 0 on
 * success and fills the out parameters. The fifo fd must be kept open for the
 * session lifetime, closing it ends the session. Returns -1 on error (error is
 * logged). */
int bus_create_session(const char *seat_id, uint32_t vtnr, uid_t uid, pid_t pid,
                       const char *desktop, const char *session_class, char *session_id_out,
                       size_t session_id_size, char *obj_out, size_t obj_size, char *runtime_out,
                       size_t runtime_size, int *fifo_fd_out);

/* Activate the specified logind session (triggers VT switch on seat0).
 * Returns 0 on success, -1 on error (error is logged). */
int bus_activate_session(const char *session_object);

/* Callback invoked by bus_enumerate_seats for each discovered seat. */
typedef void (*bus_on_seat_fn)(const char *seat_id, void *userdata);

/* Enumerate seats via logind's ListSeats() and invoke on_seat for each result.
 * Returns 0 on success, -1 on error. */
int bus_enumerate_seats(bus_on_seat_fn on_seat, void *userdata);
