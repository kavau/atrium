#pragma once

#include <stddef.h>

typedef struct {
    char **dirs;
    size_t len;
} xdg_dir_vec;

/**
 * Parse the XDG_DATA_DIR environment variable and appends to each entry '/wayland-sessions'
 * Returns A NULL terminated array of strings or NULL if the memory allocation failed.
 */
xdg_dir_vec xdg_env_get_sessions(void);

/* Helper function to free all allocated memory from xdg_env_get_sessions */
void xdg_env_free_sessions(void);
