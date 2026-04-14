#include "vt.h"

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
        fprintf(stderr, "atrium: vt_alloc: open /dev/tty0: %s\n",
                strerror(errno));
        return -1;
    }

    int vtnr = -1;
    if (ioctl(tty0, VT_OPENQRY, &vtnr) < 0) {
        fprintf(stderr, "atrium: vt_alloc: VT_OPENQRY: %s\n", strerror(errno));
        close(tty0);
        return -1;
    }
    close(tty0);

    if (vtnr <= 0) {
        fprintf(stderr, "atrium: vt_alloc: no free VTs available\n");
        return -1;
    }

    /* Open the claimed VT to hold it for the lifetime of the session. */
    char path[16];
    snprintf(path, sizeof(path), "/dev/tty%d", vtnr);
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "atrium: vt_alloc: open %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    *vtnr_out = vtnr;
    return fd;
}

void vt_release(int fd, int vtnr)
{
    /* Close the VT fd first; VT_DISALLOCATE will fail with EBUSY if any
     * process still has the VT open. */
    close(fd);

    /* Disallocate via /dev/tty0 so the kernel can reclaim the VT slot. */
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        fprintf(stderr, "atrium: vt_release: open /dev/tty0: %s\n",
                strerror(errno));
        return;
    }
    /* EBUSY means another process (e.g. a getty) still has the tty device
     * open. That's fine: close(fd) above already freed the virtual console
     * struct, which is what VT_OPENQRY watches. The tty open-count is
     * independent and does not prevent the VT slot from being reused. */
    if (ioctl(tty0, VT_DISALLOCATE, vtnr) < 0 && errno != EBUSY)
        fprintf(stderr, "atrium: vt_release: VT_DISALLOCATE vt%d: %s\n",
                vtnr, strerror(errno));
    close(tty0);
}
