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

/* Low-level IPC functions. These do not add or expect a NUL terminator. Use
ipc_recv_str() instead unless handling binary data. */
ssize_t ipc_send(ipc_channel *ch, const void *data, size_t len);
ssize_t ipc_recv(ipc_channel *ch, void *data, size_t len);

/* Send a string over the channel. The terminating NUL is not sent. */
ssize_t ipc_send_str(ipc_channel *ch, const char *msg);

/* Receive a message and NUL-terminate it. Reads at most buflen - 1 bytes.
Returns the number of bytes read (excluding the terminator) or -1 on error. */
ssize_t ipc_recv_str(ipc_channel *ch, char *buf, size_t buflen);

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

/* Return the file descriptor for reading from the IPC channel */
int ipc_get_read_fd(ipc_channel *ch);

/*
 * Typed messages
 *
 * Wire format: a type tag followed by a fixed number of NUL-terminated fields.
 * Type tags live in lib/defs.h.
 * "<type>\0<field>\0..."
 */

/* Build a message into buf. Returns its length, or -1 if buf is too small (logged). */
ssize_t ipc_msg_build1(char *buf, size_t buflen, const char *tag, const char *a);
ssize_t ipc_msg_build2(char *buf, size_t buflen, const char *tag, const char *a, const char *b);
ssize_t ipc_msg_build3(char *buf, size_t buflen, const char *tag, const char *a, const char *b,
                       const char *c);

/* Parse a message of n bytes. Returns 0 on success, -1 on error (unknown tag,
unterminated field, or incorrect field count). Fields point into buf and are
valid as long as buf is valid. */
int ipc_msg_parse1(const char *buf, ssize_t n, const char *tag, const char **a);
int ipc_msg_parse2(const char *buf, ssize_t n, const char *tag, const char **a, const char **b);
int ipc_msg_parse3(const char *buf, ssize_t n, const char *tag, const char **a, const char **b,
                   const char **c);

/* Return the type tag of a message, or NULL if it carries no NUL-terminated tag. */
const char *ipc_msg_type(const char *buf, ssize_t n);
