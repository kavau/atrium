#include "ipc.h"

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"

typedef struct ipc_channel {
    int read_fd;  /* File descriptor for reading */
    int write_fd; /* File descriptor for writing */
} ipc_channel;

ipc_channel *ipc_create(void) {
    ipc_channel *ch = malloc(sizeof(ipc_channel));
    if (!ch) {
        return NULL;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) {
        free(ch);
        return NULL;
    }
    ch->read_fd = fds[0];
    ch->write_fd = fds[1];
    return ch;
}

void ipc_set_role(ipc_channel *ch, ipc_role role) {
    switch (role) {
        case IPC_ROLE_READER:
            close(ch->write_fd); // Close the write end if we're a reader
            ch->write_fd = -1;
            break;
        case IPC_ROLE_WRITER:
            close(ch->read_fd); // Close the read end if we're a writer
            ch->read_fd = -1;
            break;
        default:
            assert(0 && "invalid ipc role");
    }
}

ssize_t ipc_send(ipc_channel *ch, const void *data, size_t len) {
    assert(ch->write_fd != -1); // Ensure we're a writer
    return write(ch->write_fd, data, len);
}

ssize_t ipc_recv(ipc_channel *ch, void *data, size_t len) {
    assert(ch->read_fd != -1); // Ensure we're a reader
    return read(ch->read_fd, data, len);
}

void ipc_close(ipc_channel *ch) {
    if (ch->read_fd != -1) {
        close(ch->read_fd);
    }
    if (ch->write_fd != -1) {
        close(ch->write_fd);
    }
    free(ch);
}
