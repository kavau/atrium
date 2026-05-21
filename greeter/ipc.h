#pragma once

/*
 * ipc.h - greeter-daemon IPC protocol
 *
 * Owns the credential pipe wire format and result parsing.
 */

#include "lib/ipc.h"

typedef enum {
    IPC_OK,
    IPC_FAIL,
} ipc_status;

/* Send credentials to the daemon. Returns 0 on success, -1 on failure. */
int ipc_send_credentials(ipc_channel *ch, const char *username, const char *password);

/* Read and parse a daemon response. Returns IPC_OK on success, IPC_FAIL on auth
or other failure. */
ipc_status ipc_read_result(ipc_channel *ch);
