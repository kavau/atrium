#pragma once

#include <stdint.h>
#include <sys/types.h>

/*
 * bus.h — D-Bus / logind interface
 */

/*
 * Callback types for logind D-Bus signal subscriptions.
 *
 * on_seat_new_fn    — called when a SeatNew signal arrives.
 * on_seat_removed_fn — called when a SeatRemoved signal arrives.
 * on_can_graphical_fn — called when CanGraphical changes on a seat;
 *                         can_graphical is 1 (true) or 0 (false).
 */
typedef void (*on_seat_new_fn)(const char *seat_id, const char *object_path);
typedef void (*on_seat_removed_fn)(const char *seat_id, const char *object_path);
typedef void (*on_can_graphical_fn)(const char *seat_id, int can_graphical);

/* Open the system bus and register its fd with the event loop.
 * Returns 0 on success, -1 on error (error is logged). */
int bus_open(void);

/* Call ListSeats() on logind and populate the seat list.
 * Returns 0 on success, -1 on error (error is logged). */
int bus_enumerate_seats(void);

/* Query the CanGraphical property for a seat by its D-Bus object path.
 * Returns 1 if graphical, 0 if not, -1 on error (error is logged). */
int bus_query_can_graphical(const char *object_path);

/* Subscribe to SeatNew and SeatRemoved signals from logind.
 * on_new and on_removed are called with (seat_id, object_path) when
 * the corresponding signal arrives.  Either callback may be NULL.
 * Returns 0 on success, -1 on error (error is logged). */
int bus_subscribe_seat_signals(on_seat_new_fn on_new,
                               on_seat_removed_fn on_removed);

/* Subscribe to PropertiesChanged on a specific seat D-Bus object.
 * on_changed is called with (seat_id, can_graphical) when CanGraphical
 * changes.  object_path is the logind seat object path.
 * Returns 0 on success, -1 on error (error is logged). */
int bus_subscribe_properties_changed(const char *seat_id,
                                     const char *object_path,
                                     on_can_graphical_fn on_changed);

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
