/*
 * ipc_test.c - Test program for IPC utilities
 *
 * Simulates a simple IPC scenario where a parent process (daemon) communicates
 * with a child process (greeter) using the IPC utilities defined in ipc.h.
 *
 * Note that in the real implementation the greeter is launched via exec(), not
 * a simple fork(). We therefore need to explicitly clear the CLOEXEC flag
 * (which is set by default for safety) on the file descriptors. This is not
 * tested in this simple tool.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/ipc.h"

/* The greeter process */
static _Noreturn void child_process(ipc_channel *ch) {
    if (ipc_send(ch, "Hello, Daemon!", 14) < 0) {
        perror("child: write");
        _exit(EXIT_FAILURE);
    }

    char buffer[256];
    ssize_t bytes_read = ipc_recv(ch, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("child: read");
        _exit(EXIT_FAILURE);
    }
    buffer[bytes_read] = '\0'; /* null-terminate the string */
    printf("Greeter received: %s\n", buffer);

    ipc_close(ch);
    _exit(EXIT_SUCCESS);
}

static int parent_process(ipc_channel *ch) {
    /* Wait for message from greeter */
    char buffer[256];
    ssize_t bytes_read = ipc_recv(ch, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("parent: read");
        return EXIT_FAILURE;
    }
    buffer[bytes_read] = '\0'; /* null-terminate the string */
    printf("Daemon received: %s\n", buffer);

    /* Send response to greeter */
    if (ipc_send(ch, "Ok bye!", 8) < 0) {
        perror("parent: write");
        return EXIT_FAILURE;
    }

    /* Wait for child process to exit */
    wait(NULL);
    printf("Daemon: Greeter has exited.\n");

    ipc_close(ch);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Create bidirectional IPC channel */
    ipc_channel *parent_end, *child_end;
    if (ipc_create(&parent_end, &child_end) < 0) {
        perror("ipc_create");
        return EXIT_FAILURE;
    }

    /* Fork child process */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        /* Child process: act as greeter */
        ipc_close(parent_end); /* close the parent's end */
        child_process(child_end);
    }

    /* Parent process: act as daemon */
    ipc_close(child_end); /* close the child's end */
    return parent_process(parent_end);
}
