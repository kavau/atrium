#include <fcntl.h>
#include <linux/vt.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "lib/log.h"
#include "vt.h"

int vt_alloc(void) {
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        log_syserr("vt_alloc: open /dev/tty0");
        return -1;
    }

    int vtnr = -1;
    if (ioctl(tty0, VT_OPENQRY, &vtnr) < 0) {
        log_syserr("vt_alloc: ioctl VT_OPENQRY");
        close(tty0);
        return -1;
    }
    close(tty0);

    if (vtnr < 0) {
        log_error("vt_alloc: no free VTs available");
        return -1;
    }

    log_info("allocated VT%d", vtnr);
    return vtnr;
}

int vt_activate(int vtnr) {
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        log_syserr("vt_activate: open /dev/tty0");
        return -1;
    }

    if (ioctl(tty0, VT_ACTIVATE, vtnr) < 0) {
        log_syserr("vt_activate: ioctl VT_ACTIVATE");
        close(tty0);
        return -1;
    }

    if (ioctl(tty0, VT_WAITACTIVE, vtnr) < 0) {
        log_syserr("vt_activate: ioctl VT_WAITACTIVE");
        close(tty0);
        return -1;
    }

    close(tty0);
    log_debug("activated VT%d", vtnr);
    return 0;
}

void vt_release(int vtnr) {
    int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (tty0 < 0) {
        log_syserr("vt_release: open /dev/tty0");
        return;
    }

    if (ioctl(tty0, VT_DISALLOCATE, vtnr) < 0) {
        /* TODO: EBUSY here is harmless, we should at most warn about it. */
        log_syserr("vt_release: ioctl VT_DISALLOCATE");
    }
    close(tty0);

    log_info("released VT%d", vtnr);
}
