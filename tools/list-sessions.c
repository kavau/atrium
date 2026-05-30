/*
 * list-sessions.c - list discovered Wayland sessions
 *
 * Usage:
 *   ./build/atrium-list-sessions
 */

#include <stdio.h>

#include "daemon/session/sessions.h"

int main(void) {
    int n = sessions_scan();
    if (n == 0) {
        printf("No Wayland sessions found.\n");
        return 1;
    }

    printf("%d session(s):\n\n", n);
    for (const session_entry *e = sessions_first(); e; e = sessions_next(e)) {
        printf("  id:            %s\n", e->id);
        printf("  name:          %s\n", e->name);
        printf("  exec:          %s\n", e->exec);
        printf("  desktop_names: %s\n", e->desktop_names ? e->desktop_names : "(none)");
        printf("\n");
    }

    sessions_free();
    return 0;
}
