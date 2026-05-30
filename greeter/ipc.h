#pragma once

/*
 * ipc.h - greeter-daemon IPC protocol
 *
 * Owns the credential pipe wire format and result parsing.
 */

#include "lib/ipc.h"

#define IPC_ERROR_INTERNAL "Internal error"

typedef enum {
    IPC_OK,
    IPC_FAIL,
} ipc_status;

/* Send credentials to the daemon. session_id is the chosen Wayland session
or "" if none was selected. Returns 0 on success, -1 on failure. */
int ipc_send_credentials(ipc_channel *ch, const char *username, const char *password,
                         const char *session_id);

/* Read and parse a daemon response. On IPC_FAIL, reason is populated with a
 * user-facing message. On IPC_OK, reason is set to an empty string. */
ipc_status ipc_read_result(ipc_channel *ch, char *reason, size_t reason_len);
