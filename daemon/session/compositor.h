#pragma once
/*
 * compositor.h - compositor child process setup
 */

#include "auth.h"
#include "sessions.h"

/* Compositor command and desktop names for a given session. Strings may point
into the session table and stay valid as long as it does. */
typedef struct {
    const char          *compositor_cmd;  /* command to exec; "" if none */
    const char          *current_desktop; /* XDG_CURRENT_DESKTOP; "" if unknown */
    const char          *session_desktop; /* XDG_SESSION_DESKTOP + DESKTOP_SESSION; "" if unknown */
    const session_entry *entry;           /* the desktop entry used, or NULL */
} session_choice;

/* Resolve compositor command and desktop names for a given session ID (a
.desktop id; empty for none) */
session_choice session_resolve(const char *session_id);

/* Build the compositor environment and exec the compositor as a login shell.
session_id identifies the chosen Wayland session (.desktop id); empty string
falls back to the compositor= config key. Called after fork(). Never returns. */
_Noreturn void child_exec_compositor(const char *username, const auth_result *pam_result,
                                     const char *session_id);
