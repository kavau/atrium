#include "daemon/session/xdg_env.h"
#include "lib/log.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define XDG_USR_SHARE \
    (path_entry) { .path = "/usr/share", .len = 10, .last_entry = true }
#define XDG_USR_LOCAL_SHARE \
    (path_entry) { .path = "/usr/local/share/wayland-sessions", .len = 16, .last_entry = true }

#define MAX_SESSION_DIRS 64

typedef struct {
    char *path;
    int   len;
    bool  last_entry;
} path_entry;

static xdg_dir_vec g_xdg_dirs = {.dirs = (char * [MAX_SESSION_DIRS]){}, .len = 0};

/* Return true if `path' is a directory, false otherwise */
static bool is_directory(char *path) {
    struct stat path_stat;

    if (path == NULL) {
        return false;
    }
    if (!stat(path, &path_stat) && path_stat.st_mode & S_IFDIR) {
        return true;
    } else {
        return false;
    }
}

static path_entry parse_entry(char *env_dirs) {
    path_entry entry;

    entry.path = NULL;
    entry.len = 0;
    entry.last_entry = false;
    if (env_dirs) {
        while (env_dirs[entry.len] && env_dirs[entry.len] != ':') {
            ++entry.len;
        }
        if (entry.len > 0) {
            entry.path = env_dirs;
        }
        if (!env_dirs[entry.len]) {
            entry.last_entry = true;
        }
    }
    return entry;
}

static void append_dir_if_valid(path_entry xdg_dir) {
    char *session_dir_path;

    if (asprintf(&session_dir_path, "%.*s/wayland-sessions", xdg_dir.len, xdg_dir.path) == -1) {
        log_error("Failed to allocate memory");
        _exit(EXIT_FAILURE);
    } else if (!is_directory(session_dir_path)) {
        log_debug("XDG directory `%.*s/wayland-sessions' doesn't exist. Skipping XDG entry.",
                  xdg_dir.len, xdg_dir.path);
        free(session_dir_path);
    } else if (g_xdg_dirs.len >= MAX_SESSION_DIRS) {
        log_error("Reached the maximum number of XDG dirs");
    } else {
        g_xdg_dirs.dirs[g_xdg_dirs.len++] = session_dir_path;
    }
}

xdg_dir_vec xdg_env_get_sessions(void) {
    char      *env_dirs;
    path_entry xdg_dir;

    env_dirs = getenv("XDG_DATA_DIRS");
    if (env_dirs != NULL) {
        xdg_dir = parse_entry(env_dirs);
        while (xdg_dir.path) {
            append_dir_if_valid(xdg_dir);
            env_dirs += xdg_dir.len + (xdg_dir.last_entry ? 0 : 1);
            xdg_dir = parse_entry(env_dirs);
        }
    }
    append_dir_if_valid(XDG_USR_SHARE);
    append_dir_if_valid(XDG_USR_LOCAL_SHARE);
    return g_xdg_dirs;
}

void xdg_env_free_sessions(void) {
    for (size_t i = 0; i < g_xdg_dirs.len; ++i) {
        free(g_xdg_dirs.dirs[i]);
        g_xdg_dirs.dirs[i] = NULL;
    }
    g_xdg_dirs.len = 0;
}
