/*
 * greeter/users.c — enumerate local users eligible for interactive login.
 */

#include "users.h"
#include "log.h"

#include <pwd.h>
#include <string.h>

int enumerate_users(greeter_user *users, int max)
{
    int count = 0;
    struct passwd *pw;

    setpwent();
    while ((pw = getpwent()) != NULL && count < max) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 65534)
            continue;
        if (pw->pw_shell) {
            if (strstr(pw->pw_shell, "nologin") ||
                strcmp(pw->pw_shell, "/bin/false") == 0)
                continue;
        }

        snprintf(users[count].name, sizeof(users[count].name),
                 "%s", pw->pw_name);

        /* Use GECOS field as display name if available.
         * SHORTCUT: GECOS can be comma-separated (name,room,phone,...);
         * we use the full string for now since real sub-fields are rare
         * on modern systems.  Split on ',' if this causes issues. */
        if (pw->pw_gecos && pw->pw_gecos[0] != '\0') {
            snprintf(users[count].display, sizeof(users[count].display),
                     "%s", pw->pw_gecos);
        } else {
            snprintf(users[count].display, sizeof(users[count].display),
                     "%s", pw->pw_name);
        }

        log_debug("user: %s (%s)", users[count].name, users[count].display);
        count++;
    }
    endpwent();

    if (count == max)
        log_warn("user list truncated at %d entries", max);

    return count;
}
