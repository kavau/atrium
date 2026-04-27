#pragma once

/*
 * auth.h - PAM user authentication
 */

#include <security/pam_appl.h>

/* Stores the results from PAM authentication and session setup.
Contents are freed in auth_close_session() */
typedef struct auth_result {
    char **env;
    pam_handle_t *pam_handle;
} auth_result;

/* Establish user credentials and open a PAM session.  conf_path may be NULL in
 * which case the default path is used. The auth result contains the session's
 * PAM handle which must be kept alive for the entire user session. Returns
 * PAM_SUCCESS (0) on success, or a nonzero error code on failure. */
int auth_open_session(const char *username, const char *password, const char **env,
                      const char *conf_path, auth_result *result);

/* Close the PAM session and free all resources. */
void auth_close_session(auth_result *result);
