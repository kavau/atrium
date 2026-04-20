#include <stdio.h>
#include <unistd.h>

/*
 * Start a hardcoded graphical user session on seat1.
 */
static int create_session(void) {
    /* Fork the child process for the user session. Must be done before
    CreateSession so we can pass the child PID to logind as the session leader.
    */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* This is the child process -- exec the compositor. */
        printf("Starting child process...\n");

        /* SHORTCUT: wait for the parent process to finish CreateSession. We
        should monitor sd_session_active() instead. */
        sleep(5);

        /* TODO:
         * - set environment variables for the session
         * - privilege drop to the user account
         * - chdir to the user home directory
         * - exec the compositor in a login shell
         */

        printf("Exec user session...\n");
        execlp("sleep", "sleep", "30", NULL);
        perror("execlp");  /* Error path - a successful exec does not return */
        return 1;
    }

    /* This is the parent process -- create the user session. */
    printf("Started child process with PID %d\n", pid);

    /*
     * TODO:
     * - open system bus
     * - call CreateSession with the child PID and seat1
     * - duplicate the session fd - it is used by logind as a lifecycle indicator
     * - wait for the child to exit, then close the session fd to end the session 
     */

    return 0;
}

int main(int argc, char **argv) {
  printf("hello atrium\n");
  return create_session();
}
