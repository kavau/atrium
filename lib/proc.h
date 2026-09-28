#pragma once

/*
 * proc.h - child process management helpers
 */

#include <unistd.h>

/* Default grace given to a process to exit before SIGKILL follows. */
#define PROC_GRACE_MS 5000

/* Send SIGTERM to pid, poll for exit up to PROC_GRACE_MS, then escalate to
SIGKILL. desc and seat_name are used only for log messages. */
void kill_and_wait(pid_t pid, const char *desc, const char *seat_name);

/* As kill_and_wait(), with the grace period given explicitly. */
void kill_and_wait_timeout(pid_t pid, int timeout_ms, const char *desc, const char *seat_name);

/* Poll for voluntary exit up to PROC_GRACE_MS, then fall back to
 * kill_and_wait(). Use on success paths where the process has already been
 * told to exit. */
void wait_and_kill(pid_t pid, const char *desc, const char *seat_name);
