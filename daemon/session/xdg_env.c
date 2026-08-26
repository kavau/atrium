#include "daemon/session/xdg_env.h"
#include "lib/log.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEFAULT_DATA_DIRS     "/usr/local/share/wayland-sessions:/usr/share/wayland-sessions"
#define DEFAULT_DATA_DIRS_LEN 61

static dir_entry_t *g_session_dirs = NULL;

static dir_entry_t *map_to_next_data_dir(char *xdg_data_dirs) {
    int       i;
    dir_entry_t *result;

    for (i = 0; xdg_data_dirs[i] && xdg_data_dirs[i] != ':'; ++i)
        ;

    if (i > 0) {
        result = malloc(sizeof(dir_entry_t));
        if (result == NULL) {
            _exit(EXIT_FAILURE);
        }
        result->next = NULL;
        result->path = NULL;
        result->path_len = asprintf(&(result->path), "%.*s/wayland-sessions", i, xdg_data_dirs);
        if (result->path_len <= 0) {
            _exit(EXIT_FAILURE);
        }
        return result;
    } else {
        return NULL;
    }
}

static void append_to_dir_list(dir_entry_t *tail) {
    dir_entry_t *cursor;

    if (g_session_dirs == NULL) {
        g_session_dirs = tail;
    } else {
        cursor = g_session_dirs;
        while (cursor->next) {
            cursor = cursor->next;
        }
        cursor->next = tail;
    }
}

dir_entry_t *xdg_env_get_sessions(void) {
    dir_entry_t *tail;
    char     *env_dirs;

    env_dirs = getenv("XDG_DATA_DIRS");
    if (env_dirs) {
        while (*env_dirs) {
            tail = map_to_next_data_dir(env_dirs);
            append_to_dir_list(tail);
            env_dirs = env_dirs + tail->path_len - 17;
            if (*env_dirs == ':') {
                ++env_dirs;
            }
        }
    }
    append_to_dir_list(map_to_next_data_dir("/usr/share"));
    append_to_dir_list(map_to_next_data_dir("/usr/local/share"));
    return g_session_dirs;
}

void xdg_env_free_sessions(void) {
    dir_entry_t *current = NULL;
    dir_entry_t *next = NULL;;

    if (g_session_dirs) {
        current = g_session_dirs;
        while (current) {
            next = current->next;
            if (current->path) {
                free(current->path);
            }
            free(current);
            current = next;
        }
    }
    g_session_dirs = NULL;
}
