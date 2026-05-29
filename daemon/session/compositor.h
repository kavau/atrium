#pragma once
/*
 * compositor.h - compositor child process setup
 */

#include "auth.h"

/* Build the compositor environment and exec the compositor as a login shell.
Called after fork(). Never returns. */
_Noreturn void child_exec_compositor(const char *username, const auth_result *pam_result);
