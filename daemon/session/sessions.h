#pragma once

/*
 * sessions.h - session discovery from .desktop files
 */

#include <stddef.h>

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

/* Find a session by id, or NULL if not found. */
const session_entry *sessions_find(const char *id);

/* Persist the session chosen by the user on seat_name so the next greeter
launch can preselect it. */
void sessions_save_seat(const char *seat_name, const char *session_id);

/* Load the previously saved session id for seat_name into buf.
If no valid saved session exists, buf is set to an empty string. */
void sessions_load_seat(const char *seat_name, char *buf, size_t buflen);
