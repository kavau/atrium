#include "daemon/session/xdg_env.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_DATA_DIRS     "/usr/local/share/wayland-sessions:/usr/share/wayland-sessions"
#define DEFAULT_DATA_DIRS_LEN 61

static dir_entry_t *g_session_dirs = NULL;

/* Read xdg_data_dir until the next ':' or null byte. `result' store the parsed dir path */
static size_t map_to_next_data_dir(dir_entry_t **result, char *xdg_data_dirs) {
    int i;

    for (i = 0; xdg_data_dirs[i] && xdg_data_dirs[i] != ':'; ++i)
        ;

    if (i > 0) {
        *result = malloc(sizeof(dir_entry_t));
        if (*result == NULL) {
            _exit(EXIT_FAILURE);
        }
        (*result)->next = NULL;
        (*result)->path = NULL;
        if (asprintf(&((*result)->path), "%.*s/wayland-sessions", i, xdg_data_dirs) <= 0) {
            _exit(EXIT_FAILURE);
        }
        return i;
    } else {
        result = NULL;
        return 0;
    }
}

/* Return 0 if `path' is a directory, a non null value otherwise */
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

/* Free the allocated memory of a dir_entry_t pointer */
static void free_entry(dir_entry_t *entry) {
    if (entry->path != NULL) {
        free(entry->path);
    }
    free(entry);
}

/* Append the `tail' to the linked list unless tail is NULL or the xdg sessions directory doesn't
 * exist */
static void append_to_dir_list(dir_entry_t *tail) {
    dir_entry_t *cursor = NULL;

    if (tail == NULL) {
        return;
    }
    if (!is_directory(tail->path)) {
        free_entry(tail);
        return;
    }
    if (g_session_dirs == NULL) {
        g_session_dirs = tail;
    } else {
        cursor = g_session_dirs;
        while (cursor->next != NULL) {
            cursor = cursor->next;
        }
        cursor->next = tail;
    }
}

dir_entry_t *xdg_env_get_sessions(void) {
    char        *env_dirs = NULL;
    int          path_len;
    dir_entry_t *tail = NULL;

    tail = NULL;
    env_dirs = getenv("XDG_DATA_DIRS");
    if (env_dirs != NULL) {
        while (*env_dirs != '\0') {
            path_len = map_to_next_data_dir(&tail, env_dirs);
            append_to_dir_list(tail);
            env_dirs = env_dirs + path_len;
            if (*env_dirs == ':') {
                ++env_dirs;
            }
        }
    }
    map_to_next_data_dir(&tail, "/usr/share");
    append_to_dir_list(tail);
    map_to_next_data_dir(&tail, "/usr/local/share");
    append_to_dir_list(tail);
    return g_session_dirs;
}

void xdg_env_free_sessions(void) {
    dir_entry_t *current = NULL;
    dir_entry_t *next = NULL;

    if (g_session_dirs != NULL) {
        current = g_session_dirs;
        while (current != NULL) {
            next = current->next;
            free_entry(current);
            current = next;
        }
    }
    g_session_dirs = NULL;
}
