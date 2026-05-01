#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "daemon/session/session_runner.h"
#include "seat.h"
#include "session.h"

int session_start(const char *username, const char *password, const char *conf_path, seat *s) {
    assert(username);
    assert(password);
    assert(s);

    /* Fork the session runner */
    pid_t runner_pid = fork();
    if (runner_pid < 0) {
        perror("fork");
        return 1;
    }

    if (runner_pid == 0) {
        /* This is the child process -- run the session setup and exec. */
        fprintf(stderr, "Starting session runner...\n");
        session_runner(username, password, conf_path, s);
    }

    /* This is the parent process -- return to the main loop. */
    fprintf(stderr, "Started session runner with PID %d for user '%s' on seat '%s'\n", runner_pid,
            username, s->name);
    return 0;
}
