#include <stdio.h>
#include <unistd.h>

#include "session.h"

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <username> [conf_path]\n", argv[0]);
        return 1;
    }

    const char *username = argv[1];
    const char *conf_path = argc == 3 ? argv[2] : NULL;
    const char *seat = "seat1";
    const char *password = getpass("Password: ");
    if (!password) {
        fprintf(stderr, "getpass failed\n");
        return 1;
    }

    return create_session(username, password, seat, conf_path);
}
