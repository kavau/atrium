#pragma once

/*
 * vt.h — Virtual Terminal allocation and keyboard management (seat0 only)
 *
 * VTs exist only on seat0. vt_alloc() finds the next free VT number via
 * VT_OPENQRY and opens /dev/ttyN to claim it. vt_release() disallocates the
 * VT and closes the fd.
 *
 * vt_suppress_keyboard() / vt_restore_keyboard() manage the VT's keyboard
 * mode to prevent keystrokes typed into a Wayland compositor from leaking
 * into the underlying TTY's input buffer.
 *
 * These functions must never be called for non-seat0 seats.
 */

/* Allocate the next free VT. On success, stores the VT number in *vtnr_out
 * and returns 0. Returns -1 on error (error is logged). */
int vt_alloc(int *vtnr_out);

/* Release a VT previously allocated by vt_alloc(). Disallocates the VT so
 * the kernel can reclaim it. */
void vt_release(int vtnr);

/*
 * vt_suppress_keyboard() — disable keyboard input on a VT.
 *
 * Opens /dev/ttyN, saves the current keyboard mode, and sets K_OFF to
 * prevent keystrokes from reaching the TTY line discipline while a Wayland
 * compositor is running on the VT.  Returns a file descriptor that must be
 * passed to vt_restore_keyboard() when the VT is done.
 *
 * Returns the tty fd (>= 0) on success, or -1 on error (error is logged).
 */
int vt_suppress_keyboard(int vtnr);

/*
 * vt_restore_keyboard() — re-enable keyboard input on a VT.
 *
 * Restores the keyboard mode saved by vt_suppress_keyboard(), flushes any
 * stale input from the TTY buffer, and closes the tty fd.
 */
void vt_restore_keyboard(int tty_fd);
