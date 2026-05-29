#pragma once
/*
 * exec.h - shared exec helpers (privilege drop, passwd environment)
 */

#include <pwd.h>

#define NUM_ENV_PASSWD 5 /* USER LOGNAME HOME SHELL PATH */

/* Look up username in the passwd database and append USER, LOGNAME, HOME,
SHELL, and PATH entries to env[i..]. Sets *pw_out on success. Returns the
updated index, or -1 on error (getpwnam failure or OOM). */
int env_append_passwd(const char *username, char **env, int i, struct passwd **pw_out);

/* Drop privileges to the given user and exec the program. Must be called after
fork(). Never returns. */
_Noreturn void drop_privs_and_exec(struct passwd *pw, const char *exe, char *const argv[],
                                   char *const env[]);
