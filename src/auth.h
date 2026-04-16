#pragma once

/*
 * auth.h — PAM (Pluggable Authentication Modules) authentication for atrium.
 *
 * Provides a two-function interface covering the full PAM login sequence:
 * authenticate the user, check account validity, open the PAM session, and
 * collect the session environment.  The PAM handle is retained in auth_result
 * so the session can be explicitly closed when the user's compositor exits.
 */

#include <sys/types.h>
#include <security/pam_appl.h>

typedef struct auth_result {
    uid_t         uid;      /* resolved via getpwnam() after successful auth */
    gid_t         gid;
    char        **pam_env;  /* from pam_getenvlist(); freed by auth_close()  */
    pam_handle_t *pamh;     /* retained for auth_close(); do not touch       */
} auth_result;

/*
 * auth_begin() — authenticate a user and open a PAM session.
 *
 * Runs the full PAM sequence in order:
 *   pam_start       — initialise the PAM library for the "atrium" service
 *   pam_authenticate — verify the password via the configured PAM modules
 *   pam_acct_mgmt   — check the account is valid, not locked, not expired
 *   pam_setcred     — establish user credentials (Kerberos tickets, etc.)
 *   pam_open_session — open the session (audit record, loginuid, keyring, …)
 *   pam_getenvlist  — collect any environment variables the modules added
 *   getpwnam        — resolve uid and gid from the username
 *
 * On success: returns PAM_SUCCESS and fills *out.  Caller must call
 * auth_close() exactly once when the session ends.
 *
 * On failure: logs the failing step, calls pam_end(), and returns the PAM
 * error code.  *out is untouched and must not be passed to auth_close().
 */
int auth_begin(const char *username, const char *password, auth_result *out);

/*
 * auth_close() — close the PAM session and free all resources.
 *
 * Calls pam_close_session() then pam_end(), then frees pam_env.
 * Must be called exactly once for each successful auth_begin().
 */
void auth_close(auth_result *result);
