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
static _Noreturn void child_process(ipc_channel *parent_to_child, ipc_channel *child_to_parent) {
    ipc_set_role(parent_to_child, IPC_ROLE_READER);
    ipc_set_role(child_to_parent, IPC_ROLE_WRITER);

    if (ipc_send(child_to_parent, "Hello, Daemon!", 14) < 0) {
        perror("child: write");
        _exit(EXIT_FAILURE);
    }

    char buffer[256];
    ssize_t bytesRead = ipc_recv(parent_to_child, buffer, sizeof(buffer) - 1);
    if (bytesRead < 0) {
        perror("child: read");
        _exit(EXIT_FAILURE);
    }
    buffer[bytesRead] = '\0'; // Null-terminate the string
    printf("Greeter received: %s\n", buffer);

    ipc_close(parent_to_child);
    ipc_close(child_to_parent);
    _exit(EXIT_SUCCESS);
}

static int parent_process(ipc_channel *parent_to_child, ipc_channel *child_to_parent) {
    ipc_set_role(parent_to_child, IPC_ROLE_WRITER);
    ipc_set_role(child_to_parent, IPC_ROLE_READER);

    /* Wait for message from greeter */
    char buffer[256];
    ssize_t bytesRead = ipc_recv(child_to_parent, buffer, sizeof(buffer) - 1);
    if (bytesRead < 0) {
        perror("parent: read");
        return EXIT_FAILURE;
    }
    buffer[bytesRead] = '\0'; // Null-terminate the string
    printf("Daemon received: %s\n", buffer);

    /* Send response to greeter */
    if (ipc_send(parent_to_child, "Ok bye!", 8) < 0) {
        perror("parent: write");
        return EXIT_FAILURE;
    }

    /* Wait for child process to exit */
    wait(NULL);
    printf("Daemon: Greeter has exited.\n");

    ipc_close(parent_to_child);
    ipc_close(child_to_parent);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Create IPC channels */
    ipc_channel *parent_to_child = ipc_create();
    if (!parent_to_child) {
        perror("ipc_create parent_to_child");
        return EXIT_FAILURE;
    }
    ipc_channel *child_to_parent = ipc_create();
    if (!child_to_parent) {
        perror("ipc_create child_to_parent");
        ipc_close(parent_to_child);
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
        child_process(parent_to_child, child_to_parent);
    }

    /* Parent process: act as daemon */
    return parent_process(parent_to_child, child_to_parent);
}
