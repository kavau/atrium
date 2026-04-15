#pragma once

#include "seat.h"

/*
 * session.h — compositor session lifecycle
 *
 * A session encompasses a logind session and the compositor process that runs
 * within it.  session_start() creates the logind session, forks the
 * compositor, and activates the session (VT switch on seat0).
 * session_stop() terminates the compositor and closes the logind session fifo.
 */

/* Start a session on the given seat.  Creates a logind session via D-Bus,
 * forks and execs the compositor as the hardcoded session user, then activates
 * the session.  Returns 0 on success, -1 on error (error is logged). */
int session_start(struct seat *s);

/* Clean up after a compositor that has already exited (SIGCHLD path).
 * Closes the logind session fifo and clears session state.  Does not
 * send any signal — the child is already dead and reaped.  Safe to call
 * when no session is active (compositor_pid == 0). */
void session_stop(struct seat *s);
