#include <fcntl.h>
#include <linux/vt.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "vt.h"

int vt_alloc(void) {
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        perror("open /dev/tty0");
        return -1;
    }

    int vtnr = ioctl(tty0, VT_OPENQRY);
    if (vtnr < 0) {
        perror("ioctl VT_OPENQRY");
        close(tty0);
        return -1;
    }
    close(tty0);

    fprintf(stderr, "Allocated VT%d\n", vtnr);
    return vtnr;
}

void vt_release(int vtnr) {
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        perror("open /dev/tty0");
        return;
    }

    if (ioctl(tty0, VT_DISALLOCATE, vtnr) < 0) {
        /* TODO: EBUSY here is harmless, we should at most warn about it. */
        perror("ioctl VT_DISALLOCATE");
    }
    close(tty0);

    fprintf(stderr, "Released VT%d\n", vtnr);
}
