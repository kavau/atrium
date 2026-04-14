#pragma once

/*
 * vt.h — Virtual Terminal allocation (seat0 only)
 *
 * VTs exist only on seat0. vt_alloc() finds the next free VT number via
 * VT_OPENQRY and opens /dev/ttyN to claim it. vt_release() disallocates the
 * VT and closes the fd.
 *
 * These functions must never be called for non-seat0 seats.
 */

/* Allocate the next free VT. On success, stores the VT number in *vtnr_out
 * and returns an open fd for /dev/ttyN (O_RDWR | O_CLOEXEC | O_NOCTTY).
 * Returns -1 on error (error is logged). */
int vt_alloc(int *vtnr_out);

/* Release a VT previously allocated by vt_alloc(). Closes the fd and
 * disallocates the VT so the kernel can reclaim it. */
void vt_release(int fd, int vtnr);
