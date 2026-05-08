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

int ipc_create(ipc_channel **end1, ipc_channel **end2) {
    ipc_channel *ch1 = malloc(sizeof(ipc_channel));
    ipc_channel *ch2 = malloc(sizeof(ipc_channel));
    if (!ch1 || !ch2) {
        log_syserr("ipc_create: malloc");
        free(ch1);
        free(ch2);
        return -1;
    }

    /* Allocate one pipe for each direction */
    int fds1[2] = {-1, -1}, fds2[2] = {-1, -1};
    if (pipe2(fds1, O_CLOEXEC) < 0 || pipe2(fds2, O_CLOEXEC) < 0) {
        log_syserr("ipc_create: pipe2");
        if (fds1[0] != -1)
            close(fds1[0]);
        if (fds1[1] != -1)
            close(fds1[1]);
        free(ch1);
        free(ch2);
        return -1;
    }
    ch1->read_fd = fds1[0];
    ch1->write_fd = fds2[1];
    ch2->read_fd = fds2[0];
    ch2->write_fd = fds1[1];

    *end1 = ch1;
    *end2 = ch2;
    return 0;
}

ssize_t ipc_send(ipc_channel *ch, const void *data, size_t len) {
    assert(ch->write_fd != -1);
    return write(ch->write_fd, data, len);
}

ssize_t ipc_recv(ipc_channel *ch, void *data, size_t len) {
    assert(ch->read_fd != -1);
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

int ipc_prepare_for_exec(ipc_channel *ch) {
    if (fcntl(ch->read_fd, F_SETFD, fcntl(ch->read_fd, F_GETFD) & ~FD_CLOEXEC) < 0) {
        log_syserr("ipc_prepare_for_exec_and_format_args: fcntl");
        return -1;
    }
    if (fcntl(ch->write_fd, F_SETFD, fcntl(ch->write_fd, F_GETFD) & ~FD_CLOEXEC) < 0) {
        log_syserr("ipc_prepare_for_exec_and_format_args: fcntl");
        return -1;
    }
    return 0;
}

void ipc_fmt_args(ipc_channel *ch, char *buf, size_t len) {
    snprintf(buf, len, "--read-fd=%d --write-fd=%d", ch->read_fd, ch->write_fd);
}

static int ipc_from_fds(ipc_channel **ch, int read_fd, int write_fd) {
    ipc_channel *channel = malloc(sizeof(ipc_channel));
    if (!channel) {
        log_syserr("ipc_from_fds: malloc");
        return -1;
    }
    channel->read_fd = read_fd;
    channel->write_fd = write_fd;
    *ch = channel;
    return 0;
}

int ipc_create_from_args(ipc_channel **ch, int argc, char *argv[]) {
    int read_fd = -1, write_fd = -1;
    for (int i = 1; i < argc; i++) {
        if (sscanf(argv[i], "--read-fd=%d", &read_fd) == 1)
            continue;
        if (sscanf(argv[i], "--write-fd=%d", &write_fd) == 1)
            continue;
    }
    if (read_fd < 0 || write_fd < 0)
        return -1;
    return ipc_from_fds(ch, read_fd, write_fd);
}
