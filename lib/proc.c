#include "proc.h"

#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"

/* Poll for process exit for up to timeout_ms. Returns 1 if exited, 0 on
timeout. Always polls at least once. */
static int poll_exit(pid_t pid, int timeout_ms, const char *desc, const char *seat_name) {
    const int POLL_MS = 50;
    int       polls = (timeout_ms + POLL_MS - 1) / POLL_MS, i = 0;
    do {
        int   wstatus = 0;
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
        usleep((useconds_t)POLL_MS * 1000);
    } while (++i < polls);
    return 0;
}

void wait_and_kill(pid_t pid, const char *desc, const char *seat_name) {
    if (poll_exit(pid, PROC_GRACE_MS, desc, seat_name))
        return;
    log_warn("%s did not exit within %d ms on seat '%s'; escalating", desc, PROC_GRACE_MS,
             seat_name);
    kill_and_wait(pid, desc, seat_name);
}

void kill_and_wait(pid_t pid, const char *desc, const char *seat_name) {
    kill_and_wait_timeout(pid, PROC_GRACE_MS, desc, seat_name);
}

void kill_and_wait_timeout(pid_t pid, int timeout_ms, const char *desc, const char *seat_name) {
    kill(pid, SIGTERM);
    if (poll_exit(pid, timeout_ms, desc, seat_name))
        return;
    log_warn("%s did not exit %d ms after SIGTERM on seat '%s'; sending SIGKILL", desc, timeout_ms,
             seat_name);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}
