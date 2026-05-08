#pragma once

/*
 * ipc.h - Inter-process communication utilities
 */

#include <stddef.h>
#include <unistd.h>

/* Holds the IPC channel between daemon and greeter. */
typedef struct ipc_channel ipc_channel;

typedef enum ipc_role {
    IPC_ROLE_READER,
    IPC_ROLE_WRITER,
} ipc_role;

/* Create a new IPC channel (returns NULL on failure) */
ipc_channel *ipc_create(void);

/* Sets the role (reader or writer) of this process using the channel */
void ipc_set_role(ipc_channel *ch, ipc_role role);

/* Low-level IPC functions */
ssize_t ipc_send(ipc_channel *ch, const void *data, size_t len);
ssize_t ipc_recv(ipc_channel *ch, void *data, size_t len);

/* Close the IPC channel */
void ipc_close(ipc_channel *ch);
