#include "users.h"

#include <pwd.h>

#include "lib/log.h"

int enumerate_users(greeter_user *users, int max) {
    int            count = 0;
    struct passwd *pw;

    setpwent();
    while ((pw = getpwent()) != NULL && count < max) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 65534)
            continue;
        if (pw->pw_shell == NULL || pw->pw_shell[0] == '\0' || strstr(pw->pw_shell, "nologin") ||
            strcmp(pw->pw_shell, "/bin/false") == 0) {
            continue;
        }
        snprintf(users[count].username, sizeof(users[count].username), "%s", pw->pw_name);
        count++;
    }
    endpwent();

    if (count == max)
        log_warn("user list truncated at %d entries", max);

    return count;
}
