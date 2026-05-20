#include "proc.h"

#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"

void kill_and_wait(pid_t pid, const char *desc, const char *seat_name) {
    const int MAX_POLLS = 100; /* 100 x 50 ms = 5 s ceiling */
    const int POLL_US   = 50000;
    kill(pid, SIGTERM);
    for (int i = 0; i < MAX_POLLS; i++) {
        int wstatus = 0;
        pid_t r = waitpid(pid, &wstatus, WNOHANG);
        if (r == pid) {
            if (WIFEXITED(wstatus))
                log_info("%s exited with status %d on seat '%s'", desc, WEXITSTATUS(wstatus),
                         seat_name);
            else if (WIFSIGNALED(wstatus))
                log_info("%s terminated by signal %d (%s) on seat '%s'", desc, WTERMSIG(wstatus),
                         strsignal(WTERMSIG(wstatus)), seat_name);
            return;
        }
        usleep((useconds_t)POLL_US);
    }
    log_warn("%s did not exit after SIGTERM on seat '%s'; sending SIGKILL", desc, seat_name);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}
