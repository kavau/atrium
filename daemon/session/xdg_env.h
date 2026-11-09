#pragma once

/*
 * xdg_env.h - locate Wayland session directories via $XDG_DATA_DIRS
 */

/* Collect the 'wayland-sessions' subdirectory of each colon separated
$XDG_DATA_DIRS entry, plus the /usr/local/share and /usr/share fallbacks.
Entries without one are skipped. */
char **xdg_env_get_session_dirs(void);

/* Free an array returned by xdg_env_get_session_dirs(); `dirs` is left dangling. */
void xdg_env_free_session_dirs(char **dirs);
