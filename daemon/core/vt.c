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

    int vtnr = -1;
    if (ioctl(tty0, VT_OPENQRY, &vtnr) < 0) {
        perror("ioctl VT_OPENQRY");
        close(tty0);
        return -1;
    }
    close(tty0);

    if (vtnr < 0) {
        fprintf(stderr, "vt_alloc: no free VTs available\n");
        return -1;
    }

    fprintf(stderr, "Allocated VT%d\n", vtnr);
    return vtnr;
}

int vt_activate(int vtnr) {
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        perror("open /dev/tty0");
        return -1;
    }

    if (ioctl(tty0, VT_ACTIVATE, vtnr) < 0) {
        perror("ioctl VT_ACTIVATE");
        close(tty0);
        return -1;
    }

    if (ioctl(tty0, VT_WAITACTIVE, vtnr) < 0) {
        perror("ioctl VT_WAITACTIVE");
        close(tty0);
        return -1;
    }

    close(tty0);
    fprintf(stderr, "Activated VT%d\n", vtnr);
    return 0;
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
