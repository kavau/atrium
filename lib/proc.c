#include "proc.h"

#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"

/* Poll for process exit up to 5 s. Returns 1 if exited, 0 on timeout. */
static int poll_exit(pid_t pid, const char *desc, const char *seat_name) {
    const int MAX_POLLS = 100; /* 100 x 50 ms = 5 s ceiling */
    const int POLL_US = 50000;
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
            return 1;
        }
        usleep((useconds_t)POLL_US);
    }
    return 0;
}

void wait_and_kill(pid_t pid, const char *desc, const char *seat_name) {
    if (poll_exit(pid, desc, seat_name))
        return;
    log_warn("%s did not exit within 5 s on seat '%s'; escalating", desc, seat_name);
    kill_and_wait(pid, desc, seat_name);
}

void kill_and_wait(pid_t pid, const char *desc, const char *seat_name) {
    kill(pid, SIGTERM);
    if (poll_exit(pid, desc, seat_name))
        return;
    log_warn("%s did not exit after SIGTERM on seat '%s'; sending SIGKILL", desc, seat_name);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}
