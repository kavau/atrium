#pragma once

/*
 * event.h — poll-based event loop
 *
 * File descriptors are registered with a callback that is invoked whenever
 * the fd becomes readable. The loop runs until event_loop_quit() is called.
 */

/* Callback invoked when a registered fd becomes readable. */
typedef void (*event_cb)(int fd, void *userdata);

/* Register fd with the event loop. cb is called with fd and userdata whenever
 * the fd is readable. Returns 0 on success, -1 on error (error is logged). */
int event_add(int fd, event_cb cb, void *userdata);

/* Remove fd from the event loop. No-op if fd is not registered. */
void event_remove(int fd);

/* Run the event loop. Blocks until event_loop_quit() is called. */
void event_loop_run(void);

/* Signal the event loop to stop after the current iteration. */
void event_loop_quit(void);
