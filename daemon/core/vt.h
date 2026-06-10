#pragma once

/*
 * vt.h - Virtual Terminal management (for seat0)
 */

/* Allocate the next free VT. Returns the VT number on success, or -1 on error. */
int vt_alloc(void);

/* Open /dev/ttyN with O_CLOEXEC. Returns the fd on success, -1 on error. */
int vt_open(int vtnr);

/* Release a VT previously allocated by vt_alloc(). */
void vt_release(int vtnr);

/* Activate the given VT. Blocks until the VT is active.
Returns 0 on success, or -1 on error. */
int vt_activate(int vtnr);

/* Suppress keyboard input on the given VT fd (KDSKBMODE K_OFF). Saves the
previous mode in *previous_mode if non-NULL. */
void vt_suppress_keyboard_fd(int fd, int *previous_mode);

/* Restore the keyboard mode on the given VT fd to previous_mode.*/
void vt_restore_keyboard_fd(int fd, int previous_mode);

/* Convenience wrappers for the above functions that take the VT number and
open/close the VT internally. */
void vt_suppress_keyboard(int vtnr, int *previous_mode);
void vt_restore_keyboard(int vtnr, int previous_mode);
