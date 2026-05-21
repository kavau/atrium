#include <stdlib.h>

#include "lib/ipc.h"
#include "lib/log.h"
#include "ui-gtk.h"
#include "users.h"

int main(void) {
    greeter_user users[MAX_NUM_USERS];
    int          num_users = enumerate_users(users, MAX_NUM_USERS);
    log_info("found %d login user(s)", num_users);

    ipc_channel *ch;
    if (ipc_create_from_env(&ch) != 0) {
        log_error("failed to create IPC channel from environment");
        return EXIT_FAILURE;
    }

    int status = run_ui(users, num_users, ch);
    log_info("greeter exiting with status %d", status);
    ipc_close(ch);
    return status;
}
