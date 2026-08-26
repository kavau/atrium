#pragma once

#include <stddef.h>

struct dir_entry {
    char            *path;
    size_t           path_len;
    struct dir_entry *next;
};

typedef struct dir_entry dir_entry_t;


/**
 * Parse the XDG_DATA_DIR environment variable and appends to each entry '/wayland-sessions'
 * Returns A NULL terminated array of strings or NULL if the memory allocation failed.
 */
dir_entry_t *xdg_env_get_sessions(void);

/* Helper function to free all allocated memory from xdg_env_get_sessions */
void xdg_env_free_sessions(void);
