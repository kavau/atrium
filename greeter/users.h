#pragma once

/*
 * users.h - user enumeration for the greeter
 *
 * Reads /etc/passwd via getpwent() and filters for uid >= 1000, uid < 65534,
 * with a valid login shell.
 */

#define MAX_USERNAME_LEN     64
#define MAX_DISPLAY_NAME_LEN 256

typedef struct {
    char username[MAX_USERNAME_LEN];         /* pw_name */
    char display_name[MAX_DISPLAY_NAME_LEN]; /* real name from GECOS, or username if empty */
    int  passwordless;                       /* 1 if user is in the nopasswdlogin group */
} greeter_user;

/* Populate users with eligible accounts. Returns the number of users populated. */
int enumerate_users(greeter_user *users, int max);
