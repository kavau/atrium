#pragma once

/*
 * proc.h - child process management helpers
 */

#include <unistd.h>

/* Send SIGTERM to pid, poll for exit up to 5 s, then escalate to SIGKILL.
desc and seat_name are used only for log messages. */
void kill_and_wait(pid_t pid, const char *desc, const char *seat_name);

/* Poll for voluntary exit up to 5 s, then send SIGKILL if still running.
 * Use on success paths where the process has already been told to exit. */
void wait_and_kill(pid_t pid, const char *desc, const char *seat_name);
