#pragma once

/*
 * session_runner.h - session lifecycle
 */

#include "daemon/core/seat.h"

/* Run the full greeter + user session lifecycle for the given seat. This
function is itself executed as a child process of the daemon. It exits when the
lifecycle completes (greeter ran, user authenticated, compositor ran and
exited). */
_Noreturn void session_runner(const char *pam_conf_path, const seat *s);
