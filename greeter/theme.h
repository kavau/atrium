#pragma once

/*
 * theme.h - greeter CSS theme loading
 */

/* Load and apply the default theme to the current display. Must be called after
the GdkDisplay is available (i.e. from 'activate'). */
void theme_apply(void);
