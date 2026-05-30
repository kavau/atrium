#include <stdlib.h>
#include <string.h>

#include "lib/ipc.h"
#include "lib/log.h"
#include "ui-gtk.h"
#include "users.h"

static void copy_bounded(char *dst, size_t dstsize, const char *src, size_t len) {
    if (len >= dstsize)
        len = dstsize - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Parse ATRIUM_SESSION_LIST ("id\x1fname\x1e..." pairs) into sessions[].
Returns the number of entries written (capped at max). */
static int parse_session_list(greeter_session *sessions, int max) {
    const char *env = getenv("ATRIUM_SESSION_LIST");
    if (!env || !*env)
        return 0;

    int   count = 0;
    const char *p = env;
    while (*p && count < max) {
        const char *us = strchr(p, '\x1f'); /* unit separator */
        if (!us)
            break;
        const char *rs = strchr(us + 1, '\x1e'); /* record separator */
        if (!rs)
            break;

        copy_bounded(sessions[count].id,   sizeof(sessions[count].id),
                     p,      (size_t)(us - p));
        copy_bounded(sessions[count].name, sizeof(sessions[count].name),
                     us + 1, (size_t)(rs - us - 1));

        count++;
        p = rs + 1;
    }
    return count;
}

int main(void) {
    greeter_user users[MAX_NUM_USERS];
    int          num_users = enumerate_users(users, MAX_NUM_USERS);
    log_info("found %d login user(s)", num_users);

    greeter_session sessions[MAX_NUM_SESSIONS];
    int             num_sessions = parse_session_list(sessions, MAX_NUM_SESSIONS);
    log_info("found %d session(s)", num_sessions);

    ipc_channel *ch;
    if (ipc_create_from_env(&ch) != 0) {
        log_error("failed to create IPC channel from environment");
        return EXIT_FAILURE;
    }

    int status = run_ui(users, num_users, sessions, num_sessions, ch);
    log_info("greeter exiting with status %d", status);
    ipc_close(ch);
    return status;
}
