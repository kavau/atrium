#include "lock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/log.h"

#define LOCK_DIR "/var/lib/atrium"

/* Lock fd for the active session; -1 if none held. */
static int g_lock_fd = -1;

login_lock_status acquire_login_lock(uid_t uid) {
    /* Defensively create lock directory if it doesn't exist. */
    if (mkdir(LOCK_DIR, 0700) < 0 && errno != EEXIST) {
        log_syserr("mkdir %s", LOCK_DIR);
        return LOGIN_LOCK_ERROR;
    }

    char lockpath[256];
    snprintf(lockpath, sizeof(lockpath), "%s/uid-%u.lock", LOCK_DIR, uid);

    int fd = open(lockpath, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) {
        log_syserr("open login lock %s", lockpath);
        return LOGIN_LOCK_ERROR;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        int err = errno;
        close(fd);

        if (err == EWOULDBLOCK) {
            log_info("login lock held for uid %u (duplicate login attempt)", uid);
            return LOGIN_LOCK_DUPLICATE;
        }

        log_syserr("flock login lock %s", lockpath);
        return LOGIN_LOCK_ERROR;
    }

    g_lock_fd = fd;
    return LOGIN_LOCK_OK;
}

void release_login_lock(void) {
    if (g_lock_fd >= 0) {
        close(g_lock_fd);
        g_lock_fd = -1;
    }
}
