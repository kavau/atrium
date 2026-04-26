#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "session.h"
#include "vt.h"

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s <username> <seat> [conf_path]\n", argv[0]);
        return 1;
    }

    const char *username = argv[1];
    const char *seat = argv[2];
    const char *conf_path = argc == 4 ? argv[3] : NULL;
    const char *password = getpass("Password: ");
    if (!password) {
        fprintf(stderr, "getpass failed\n");
        return 1;
    }

    /* Allocate a VT for seat0. */
    int vtnr = 0;
    if (strcmp(seat, "seat0") == 0) {
        vtnr = vt_alloc();
        if (vtnr < 0) {
            fprintf(stderr, "Failed to allocate VT: %d\n", vtnr);
            return EXIT_FAILURE;
        }
        /* TODO: suppress VT keyboard so keystrokes typed into the Wayland
        session don't leak into the TTY's input buffer. */
    }

    int r = create_session(username, password, seat, vtnr, conf_path);
    if (vtnr > 0) {
        vt_release(vtnr);
    }
    return r;
}
