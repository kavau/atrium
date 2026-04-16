#include "vt.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*
 * Saved keyboard mode from vt_suppress_keyboard().  A single static int
 * suffices because atrium only allocates one VT (for seat0).
 */
static int saved_kb_mode = -1;

int vt_alloc(int *vtnr_out)
{
    /* Open /dev/tty0 to issue VT_OPENQRY, which returns the number of the
     * next free (unallocated) VT. */
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        log_error("vt_alloc: open /dev/tty0: %s", strerror(errno));
        return -1;
    }

    int vtnr = -1;
    if (ioctl(tty0, VT_OPENQRY, &vtnr) < 0) {
        log_error("vt_alloc: VT_OPENQRY: %s", strerror(errno));
        close(tty0);
        return -1;
    }
    close(tty0);

    if (vtnr <= 0) {
        log_error("vt_alloc: no free VTs available");
        return -1;
    }

    *vtnr_out = vtnr;
    log_debug("allocated vt%d", vtnr);
    return 0;
}

void vt_release(int vtnr)
{
    log_debug("releasing vt%d", vtnr);
    /* Disallocate via /dev/tty0 so the kernel can reclaim the VT slot. */
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        log_error("vt_release: open /dev/tty0: %s", strerror(errno));
        return;
    }
    /* EBUSY means another process (e.g. a getty) still has the tty device
     * open. That's fine: close(fd) above already freed the virtual console
     * struct, which is what VT_OPENQRY watches. The tty open-count is
     * independent and does not prevent the VT slot from being reused. */
    if (ioctl(tty0, VT_DISALLOCATE, vtnr) < 0 && errno != EBUSY)
        log_error("vt_release: VT_DISALLOCATE vt%d: %s", vtnr, strerror(errno));
    close(tty0);
}

int vt_suppress_keyboard(int vtnr)
{
    char tty_path[32];
    snprintf(tty_path, sizeof(tty_path), "/dev/tty%d", vtnr);

    int fd = open(tty_path, O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        log_error("vt_suppress_keyboard: open %s: %s",
                  tty_path, strerror(errno));
        return -1;
    }

    /* Save the current keyboard mode so we can restore it later. */
    int kb_mode;
    if (ioctl(fd, KDGKBMODE, &kb_mode) < 0) {
        log_error("vt_suppress_keyboard: KDGKBMODE %s: %s",
                  tty_path, strerror(errno));
        close(fd);
        return -1;
    }
    saved_kb_mode = kb_mode;

    /* K_OFF: kernel drops all keyboard input for this VT.  No characters
     * reach the TTY line discipline, preventing password keystrokes from
     * being buffered while a Wayland compositor reads input via libinput. */
    if (ioctl(fd, KDSKBMODE, K_OFF) < 0) {
        log_error("vt_suppress_keyboard: KDSKBMODE K_OFF %s: %s",
                  tty_path, strerror(errno));
        close(fd);
        return -1;
    }

    /* Flush any input already buffered before we set K_OFF. */
    tcflush(fd, TCIFLUSH);

    log_debug("vt%d: keyboard suppressed (saved mode %d)", vtnr, kb_mode);
    return fd;
}

void vt_restore_keyboard(int tty_fd)
{
    if (tty_fd < 0)
        return;

    /* Flush any stale input that may have accumulated despite K_OFF
     * (e.g. from a race at mode transition boundaries). */
    tcflush(tty_fd, TCIFLUSH);

    /* Restore the keyboard mode saved by vt_suppress_keyboard(). */
    if (saved_kb_mode >= 0) {
        if (ioctl(tty_fd, KDSKBMODE, saved_kb_mode) < 0)
            log_error("vt_restore_keyboard: KDSKBMODE %d: %s",
                      saved_kb_mode, strerror(errno));
        else
            log_debug("vt keyboard restored to mode %d", saved_kb_mode);
        saved_kb_mode = -1;
    }

    close(tty_fd);
}
