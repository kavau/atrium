#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
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

    while (ioctl(tty0, VT_WAITACTIVE, vtnr) < 0) {
        if (errno != EINTR) {
            log_syserr("vt_activate: ioctl VT_WAITACTIVE");
            close(tty0);
            return -1;
        }
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
        /* EBUSY means the VT is still open (e.g. a process hasn't exited yet);
        the kernel will free it once the last fd is closed. */
        if (errno == EBUSY)
            log_warn("vt_release: VT%d is busy, will be freed when last fd closes", vtnr);
        else
            log_syserr("vt_release: ioctl VT_DISALLOCATE");
    }
    close(tty0);

    log_info("released VT%d", vtnr);
}

int vt_open(int vtnr) {
    char path[16];
    snprintf(path, sizeof(path), "/dev/tty%d", vtnr);
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (fd < 0)
        log_syserr("vt_open: open %s", path);
    return fd;
}

void vt_suppress_keyboard_fd(int fd, int *previous_mode) {
    if (fd < 0)
        return;
    if (previous_mode) {
        if (ioctl(fd, KDGKBMODE, previous_mode) < 0) {
            log_syserr("vt_suppress_keyboard_fd: ioctl KDGKBMODE");
            *previous_mode = K_UNICODE; /* safe default */
        }
    }
    if (ioctl(fd, KDSKBMODE, K_OFF) < 0)
        log_syserr("vt_suppress_keyboard_fd: ioctl KDSKBMODE K_OFF");
}

void vt_restore_keyboard_fd(int fd, int previous_mode) {
    if (fd < 0)
        return;
    if (ioctl(fd, KDSKBMODE, previous_mode) < 0)
        log_syserr("vt_restore_keyboard_fd: ioctl KDSKBMODE");
}

void vt_suppress_keyboard(int vtnr, int *previous_mode) {
    int fd = vt_open(vtnr);
    if (fd >= 0) {
        vt_suppress_keyboard_fd(fd, previous_mode);
        close(fd);
    }
}

void vt_restore_keyboard(int vtnr, int previous_mode) {
    int fd = vt_open(vtnr);
    if (fd >= 0) {
        vt_restore_keyboard_fd(fd, previous_mode);
        close(fd);
    }
}
