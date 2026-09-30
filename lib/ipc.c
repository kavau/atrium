#include "ipc.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

ssize_t ipc_send_str(ipc_channel *ch, const char *msg) { return ipc_send(ch, msg, strlen(msg)); }

ssize_t ipc_recv(ipc_channel *ch, void *data, size_t len) {
    assert(ch->read_fd != -1);
    return read(ch->read_fd, data, len);
}

ssize_t ipc_recv_str(ipc_channel *ch, char *buf, size_t buflen) {
    assert(buf);
    assert(buflen > 0); /* buf[0] is always written */
    ssize_t n = ipc_recv(ch, buf, buflen - 1);
    buf[n > 0 ? (size_t)n : 0] = '\0';
    return n;
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

int ipc_get_read_fd(ipc_channel *ch) { return ch->read_fd; }

char **ipc_getenvlist(ipc_channel *ch) {
    char **list = malloc(3 * sizeof(char *));
    if (!list) {
        log_syserr("ipc_getenvlist: malloc");
        return NULL;
    }
    if (asprintf(&list[0], "CREDENTIALS_FD=%d", ch->write_fd) < 0) {
        log_syserr("ipc_getenvlist: asprintf");
        free(list);
        return NULL;
    }
    if (asprintf(&list[1], "RESULT_FD=%d", ch->read_fd) < 0) {
        log_syserr("ipc_getenvlist: asprintf");
        free(list[0]);
        free(list);
        return NULL;
    }
    list[2] = NULL;
    return list;
}

int ipc_create_from_env(ipc_channel **ch) {
    const char *creds = getenv("CREDENTIALS_FD");
    const char *result = getenv("RESULT_FD");
    if (!creds || !result) {
        log_error("ipc_create_from_env: CREDENTIALS_FD or RESULT_FD not set");
        return -1;
    }
    int write_fd = -1, read_fd = -1;
    if (sscanf(creds, "%d", &write_fd) != 1 || write_fd < 0 ||
        sscanf(result, "%d", &read_fd) != 1 || read_fd < 0) {
        log_error("ipc_create_from_env: invalid fd values");
        return -1;
    }
    return ipc_from_fds(ch, read_fd, write_fd);
}

/* Concatenate tag and count fields, each NUL-terminated. */
static ssize_t build_fields(char *buf, size_t buflen, const char *tag, const char *const *fields,
                            int count) {
    size_t pos = 0;
    for (int i = -1; i < count; i++) {
        const char *s = (i < 0) ? tag : fields[i];
        size_t      len = strlen(s) + 1; /* include the NUL */
        if (len > buflen - pos) {
            log_error("ipc_msg_build: message does not fit in %zu bytes", buflen);
            return -1;
        }
        memcpy(buf + pos, s, len);
        pos += len;
    }
    return (ssize_t)pos;
}

/* Split a message into count NUL-terminated fields after the tag. Rejects a
wrong tag, an unterminated field, and any trailing bytes. */
static int parse_fields(const char *buf, ssize_t n, const char *tag, const char **fields,
                        int count) {
    if (n <= 0)
        return -1;
    size_t left = (size_t)n;
    size_t pos = 0;
    for (int i = -1; i < count; i++) {
        size_t len = strnlen(buf + pos, left);
        if (len >= left)
            return -1; /* missing NUL: message is truncated or malformed */
        if (i < 0) {
            if (strcmp(buf, tag) != 0)
                return -1;
        } else {
            fields[i] = buf + pos;
        }
        pos += len + 1;
        left -= len + 1;
    }
    return left == 0 ? 0 : -1; /* more fields than expected */
}

ssize_t ipc_msg_build1(char *buf, size_t buflen, const char *tag, const char *a) {
    const char *f[] = {a};
    return build_fields(buf, buflen, tag, f, 1);
}

ssize_t ipc_msg_build2(char *buf, size_t buflen, const char *tag, const char *a, const char *b) {
    const char *f[] = {a, b};
    return build_fields(buf, buflen, tag, f, 2);
}

ssize_t ipc_msg_build3(char *buf, size_t buflen, const char *tag, const char *a, const char *b,
                       const char *c) {
    const char *f[] = {a, b, c};
    return build_fields(buf, buflen, tag, f, 3);
}

int ipc_msg_parse1(const char *buf, ssize_t n, const char *tag, const char **a) {
    const char *f[1];
    if (parse_fields(buf, n, tag, f, 1) < 0)
        return -1;
    *a = f[0];
    return 0;
}

int ipc_msg_parse2(const char *buf, ssize_t n, const char *tag, const char **a, const char **b) {
    const char *f[2];
    if (parse_fields(buf, n, tag, f, 2) < 0)
        return -1;
    *a = f[0];
    *b = f[1];
    return 0;
}

int ipc_msg_parse3(const char *buf, ssize_t n, const char *tag, const char **a, const char **b,
                   const char **c) {
    const char *f[3];
    if (parse_fields(buf, n, tag, f, 3) < 0)
        return -1;
    *a = f[0];
    *b = f[1];
    *c = f[2];
    return 0;
}

const char *ipc_msg_type(const char *buf, ssize_t n) {
    if (n <= 0 || strnlen(buf, (size_t)n) >= (size_t)n)
        return NULL;
    return buf;
}
