#pragma once

/*
 * ipc.h - Inter-process communication utilities
 */

#include <stddef.h>
#include <unistd.h>

/* A bidirectional IPC channel */
typedef struct ipc_channel ipc_channel;

/* Create a new bidirectional IPC channel - keep one end and pass the other one
to the peer. After fork(), each process should immediately close the end it
doesn't use. Returns 0 on success, -1 on failure. */
int ipc_create(ipc_channel **end1, ipc_channel **end2);

/* Low-level IPC functions */
ssize_t ipc_send(ipc_channel *ch, const void *data, size_t len);
ssize_t ipc_recv(ipc_channel *ch, void *data, size_t len);

/* Close the IPC channel */
void ipc_close(ipc_channel *ch);

/* Prepare the IPC channel for exec() by clearing the default FD_CLOEXEC flag.
Returns 0 on success, -1 on failure. */
int ipc_prepare_for_exec(ipc_channel *ch);

/* Return a list of environment variables for re-creating the IPC channel via
ipc_create_from_env(). Caller owns the array and the strings.
Returns NULL on allocation failure. */
char **ipc_getenvlist(ipc_channel *ch);

/* Create a bidirectional IPC channel from existing file descriptors supplied
via environment variables. Returns 0 on success, -1 on failure. */
int ipc_create_from_env(ipc_channel **ch);
