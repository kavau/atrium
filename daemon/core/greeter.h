#pragma once

#include "seat.h"

/*
 * greeter.h
 */

/* Start a greeter on the given seat. Launches a session runner child process,
 * which takes over the greeter lifecycle management. pam_conf_path is the PAM
 * configuration directory (may be NULL for default).
 * Returns 0 on success, 1 on failure. */
int greeter_start(const char *pam_conf_path, seat *s);
