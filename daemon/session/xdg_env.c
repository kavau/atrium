#include "daemon/session/xdg_env.h"
#include "lib/log.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define XDG_USR_SHARE       "/usr/share"
#define XDG_USR_LOCAL_SHARE "/usr/local/share"

#define MAX_SESSION_DIRS 64

/* One slot beyond the cap keeps the list NULL-terminated at all times. */
#define DIRS_SLOTS (MAX_SESSION_DIRS + 1)

/* Return true if `path' is a directory, false otherwise */
static bool is_directory(const char *path) {
    struct stat path_stat;

    return !stat(path, &path_stat) && S_ISDIR(path_stat.st_mode);
}

/* The first NULL marks the end of the list. */
static size_t dir_count(char *const *dirs) {
    size_t n = 0;

    while (dirs[n] != NULL)
        n++;
    return n;
}

static void append_dir_if_exists(char **dirs, const char *path, size_t len) {
    char  *session_dir_path;
    size_t n = dir_count(dirs);

    if (n >= MAX_SESSION_DIRS) {
        log_error("Reached the maximum number of XDG dirs");
        return;
    }
    if (asprintf(&session_dir_path, "%.*s/wayland-sessions", (int)len, path) == -1) {
        log_error("Failed to allocate memory");
        _exit(EXIT_FAILURE);
    }
    if (!is_directory(session_dir_path)) {
        log_debug("XDG directory `%.*s/wayland-sessions' doesn't exist. Skipping XDG entry.",
                  (int)len, path);
        free(session_dir_path);
        return;
    }
    dirs[n] = session_dir_path;
}

char **xdg_env_get_session_dirs(void) {
    char      **dirs = calloc(DIRS_SLOTS, sizeof(*dirs));
    const char *env_dirs;

    if (dirs == NULL) {
        log_error("Failed to allocate memory");
        _exit(EXIT_FAILURE);
    }
    env_dirs = getenv("XDG_DATA_DIRS");
    for (const char *p = env_dirs; p && *p;) {
        size_t len = strcspn(p, ":");
        if (len > 0)
            append_dir_if_exists(dirs, p, len);
        p += len;
        if (*p == ':')
            p++;
    }
    append_dir_if_exists(dirs, XDG_USR_LOCAL_SHARE, strlen(XDG_USR_LOCAL_SHARE));
    append_dir_if_exists(dirs, XDG_USR_SHARE, strlen(XDG_USR_SHARE));
    return dirs;
}

void xdg_env_free_session_dirs(char **dirs) {
    if (dirs == NULL)
        return;
    for (size_t i = 0; dirs[i] != NULL; ++i)
        free(dirs[i]);
    free(dirs);
}
