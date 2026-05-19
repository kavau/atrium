#pragma once

/*
 * vt.h - Virtual Terminal management (for seat0)
 */

/* Allocate the next free VT. Returns the VT number on success, or -1 on error. */
int vt_alloc(void);

/* Release a VT previously allocated by vt_alloc(). */
void vt_release(int vtnr);

/* Activate the given VT. Blocks until the VT is active.
Returns 0 on success, or -1 on error. */
int vt_activate(int vtnr);

/* Suppress keyboard input on the given VT to prevent keystrokes from
accumulating in the TTY buffer while a Wayland session is running. */
void vt_suppress_keyboard(int vtnr);

/* Restore keyboard input on the given VT to the default mode (K_UNICODE). */
void vt_restore_keyboard(int vtnr);
