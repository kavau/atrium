#pragma once

/*
 * auth.h — PAM (Pluggable Authentication Modules) authentication for atrium.
 *
 * The PAM sequence is split across two processes:
 *   auth_begin()        — daemon: authenticate + account check + getpwnam
 *   auth_open_session() — child:  setcred + open_session + getenvlist
 *   auth_close()        — daemon: close_session + delete_cred + end
 *
 * This split ensures session modules (pam_loginuid, pam_keyinit, etc.) run
 * in the compositor's PID context, not the daemon's.
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
 * auth_begin() — authenticate a user and verify account validity.
 *
 * Runs the first half of the PAM sequence (in the daemon process):
 *   pam_start        — initialise the PAM library for the "atrium" service
 *   pam_authenticate — verify the password via the configured PAM modules
 *   pam_acct_mgmt   — check the account is valid, not locked, not expired
 *   getpwnam         — resolve uid and gid from the username
 *
 * On success: returns PAM_SUCCESS and fills *out (uid, gid, pamh).
 * Caller must then call auth_open_session() in the child process and
 * auth_close() in the daemon when the session ends.
 *
 * On failure: logs the failing step, calls pam_end(), and returns the PAM
 * error code.  *out is untouched and must not be passed to auth_close().
 */
int auth_begin(const char *username, const char *password, auth_result *out);

/*
 * auth_open_session() — establish credentials and open the PAM session.
 *
 * Runs the second half of the PAM sequence (in the forked child, as root,
 * before privilege drop and exec):
 *   pam_setcred      — establish user credentials (Kerberos tickets, etc.)
 *   pam_open_session — session setup (audit log, loginuid, keyring, limits)
 *   pam_getenvlist   — collect environment variables set by PAM modules
 *
 * Must be called exactly once after a successful auth_begin(), in the child
 * process.  On success returns PAM_SUCCESS and fills result->pam_env.
 * On failure returns the PAM error code (logged).
 */
int auth_open_session(auth_result *result);

/*
 * auth_close() — close the PAM session and free all resources.
 *
 * Calls pam_close_session() then pam_end(), then frees pam_env.
 * Called in the daemon (parent) after the compositor exits.
 */
void auth_close(auth_result *result);
