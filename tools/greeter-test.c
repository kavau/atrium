/*
 * greeter-test.c - standalone test for greeter UI and IPC.
 *
 * Takes the role of the daemon: sets up an IPC channel, launches the greeter,
 * and simulates the conversation flow.
 */

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/ipc.h"
#include "lib/log.h"
#include "lib/proc.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        log_error("Usage: %s <greeter-executable>", argv[0]);
        return EXIT_FAILURE;
    }

    /* Create IPC channel for the greeter to communicate credentials. */
    ipc_channel *parent_end, *child_end;
    if (ipc_create(&parent_end, &child_end) < 0) {
        log_syserr("ipc_create");
        _exit(EXIT_FAILURE);
    }

    char **ipc_env = ipc_getenvlist(child_end);
    if (!ipc_env) {
        log_error("out of memory");
        return EXIT_FAILURE;
    }
    for (char **p = ipc_env; *p; p++)
        putenv(*p);
    if (ipc_prepare_for_exec(child_end) < 0) {
        log_syserr("ipc_prepare_for_exec");
        return EXIT_FAILURE;
    }

    pid_t greeter_pid = fork();
    if (greeter_pid < 0) {
        log_syserr("fork");
        return EXIT_FAILURE;
    }
    if (greeter_pid == 0) {
        /* child process */
        ipc_close(parent_end);
        log_info("greeter child process started (PID %d)", (int)getpid());
        execl(argv[1], argv[1], NULL);
        log_syserr("execl");
        _exit(EXIT_FAILURE);
    }

    /* parent process */
    ipc_close(child_end);
    log_info("parent process: launched greeter with PID %d", (int)greeter_pid);

    /* Two rounds of conversation: send a failure message in the first round,
    then ok in the second round */
    for (int i = 0; i < 2; i++) {
        log_info("parent process: waiting for message from greeter...");
        char buf[512];
        ssize_t n = ipc_recv(parent_end, buf, sizeof(buf) - 1);
        if (n <= 0) {
            log_error("greeter disconnected");
            kill_and_wait(greeter_pid, "greeter", "seat0");
            ipc_close(parent_end);
            return EXIT_FAILURE;
        }
        buf[n] = '\0';
        log_info("received from greeter: %s", buf);

        const char *response = (i == 0) ? "fail:invalid credentials\n" : "ok\n";
        log_info("sending to greeter: %s", response);
        if (ipc_send_str(parent_end, response) < 0) {
            log_syserr("ipc_send_str");
            kill_and_wait(greeter_pid, "greeter", "seat0");
            ipc_close(parent_end);
            return EXIT_FAILURE;
        }
    }

    /* Cleanup */

    /* TODO: use wait_and_kill() from proc.h instead of waitpid() */
    if (waitpid(greeter_pid, NULL, 0) < 0) {
        log_syserr("waitpid");
        return EXIT_FAILURE;
    }
    ipc_close(parent_end);

    return EXIT_SUCCESS;
}