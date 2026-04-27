#pragma once

/*
 * session.h - session lifecycle
 */

#include "seat.h"

/* Start a session for the given user on the given seat. conf_path is the PAM
 * configuration directory (may be NULL for default). */
int session_start(const char *username, const char *password, const char *conf_path, seat *s);

/* Stop the session for the given seat. */
void session_stop(seat *s);
