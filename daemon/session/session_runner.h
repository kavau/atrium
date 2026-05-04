#pragma once

/*
 * session_runner.h - session lifecycle
 */

#include "daemon/core/seat.h"

typedef enum {
    SESSION_GREETER,
    SESSION_USER,
} session_type;

/* Create a logind session and launch a compositor child process for a session
of the specified type (greeter or user). This function is itself executed as a
child process of the daemon - the session runner. It exits when the session has
completed. */
_Noreturn void session_runner(const char *username, const char *password, const char *pam_conf_path,
                              const seat *s, session_type type);
