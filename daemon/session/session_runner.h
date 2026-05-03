#pragma once

/*
 * session.h - session lifecycle
 */

#include "daemon/core/seat.h"

/* Create the logind session and launch the compositor child process. This
function is itself executed as a child process of the daemon - the session
runner. It exits when the session has completed. */
_Noreturn void session_runner(const char *username, const char *password, const char *pam_conf_path,
                              const seat *s);
