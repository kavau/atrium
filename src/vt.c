#include "vt.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/vt.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

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
