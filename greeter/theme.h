#pragma once

/*
 * theme.h - greeter CSS theme loading
 */

/* Load and apply the default theme to the current display. Must be called after
the GdkDisplay is available (i.e. from 'activate'). */
void theme_apply(void);

/* Resolve the configured background-image path to a concrete file (picks a
random image if the path is a directory). Returns a g_malloc'd string, or NULL
if no background is configured. Caller must g_free() the result. */
char *theme_resolve_background_path(void);
