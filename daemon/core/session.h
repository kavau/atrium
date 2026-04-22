#pragma once

/*
 * session.h - session lifecycle
 */

/* Start a session for the given user on the given seat.
 * SHORTCUT: must not be seat0.
 * SHORTCUT: blocks until the session ends. */
int create_session(int uid, const char *seat);