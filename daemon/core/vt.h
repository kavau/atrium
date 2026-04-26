#pragma once

/*
 * vt.h - Virtual Terminal management (for seat0)
 */

/* Allocate the next free VT. Returns the VT number on success, or -1 on error. */
int vt_alloc(void);

/* Release a VT previously allocated by vt_alloc(). */
void vt_release(int vtnr);
