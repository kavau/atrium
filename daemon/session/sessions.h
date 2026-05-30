#pragma once

/*
 * sessions.h - session discovery from .desktop files
 */

typedef struct {
    char *id;            /* filename without .desktop suffix */
    char *name;          /* Name= from the desktop entry */
    char *exec;          /* Exec= from the desktop entry */
    char *desktop_names; /* DesktopNames= with ';' converted to ':'; NULL if absent */
} session_entry;

/* Scan wayland-sessions directories and load all valid entries. Returns the
number of sessions found. Can safely be called again to refresh the list. */
int sessions_scan(void);

/* Free all session entries. */
void sessions_free(void);

/* Session iteration - return the first session, or NULL if none were found. */
const session_entry *sessions_first(void);

/* Session iteration - return the next session or NULL. */
const session_entry *sessions_next(const session_entry *s);

/* Find a session by ID, or NULL if not found. */
const session_entry *sessions_find(const char *id);
