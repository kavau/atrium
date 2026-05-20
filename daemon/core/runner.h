#pragma once

#include "seat.h"

/*
 * runner.h - session runner management
 */

/* Fork the session runner for the given seat. The runner handles the full
 * greeter + user session lifecycle. Returns 0 on success, 1 on failure. */
int runner_start(const char *pam_conf_path, seat *s);

/* Stop the session runner. Sends SIGTERM and escalates to SIGKILL after 5 s. */
void runner_stop(seat *s);
