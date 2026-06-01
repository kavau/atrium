#pragma once
/*
 * greeter.h - greeter child process setup
 */

#include "daemon/core/seat.h"
#include "lib/ipc.h"

/* Build the greeter environment and exec the greeter. Blocks on ipc_recv until
the parent sends the session_id. Called after fork(). Never returns. */
_Noreturn void child_exec_greeter(const char *username, const seat *s, ipc_channel *ch,
                                  const char *session_list, const char *preselect);
