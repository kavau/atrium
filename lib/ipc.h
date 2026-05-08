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

/* Format command line arguments for passing the IPC channel to a child
process. The arguments can be parsed by ipc_create_from_args(). */
void ipc_fmt_args(ipc_channel *ch, char *buf, size_t len);

/* Create a bidirectional IPC channel from existing file descriptors supplied
via command line arguments (e.g., --read-fd=3 --write-fd=4).
Returns 0 on success, -1 on failure. */
int ipc_create_from_args(ipc_channel **ch, int argc, char *argv[]);