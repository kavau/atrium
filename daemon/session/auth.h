#pragma once

/*
 * auth.h - PAM user authentication
 *
 * Authentication is a two-phase process:
 * 1. auth_authenticate: verify the username and password, but do not open a
 *    session yet. If this succeeds, either auth_open_session or auth_cancel
 *    must be called.
 * 2. auth_open_session: create the systemd-logind session. Once the user
 *    session ends, auth_close_session must be called.
 */

#include <security/pam_appl.h>

/* Stores PAM state across the auth flow. Owned by the caller. Contents are
freed by auth_open_session (on failure), auth_cancel, or auth_close_session. */
typedef struct auth_result {
    char        **env;
    pam_handle_t *pam_handle;
} auth_result;

/* Phase 1: Establish user credentials. Sets result->pam_handle on success. The
PAM handle must be kept alive for the entire user session. pam_conf_path may be
NULL, in which case the default path is used.
Returns PAM_SUCCESS (0) on success, or a nonzero error code on failure. */
int auth_authenticate(const char *username, const char *password, const char **env,
                      const char *pam_conf_path, const char *service_name, auth_result *result);

/* Phase 2: Open the user session. Populates result->env on success. The caller
must call auth_close_session when the session ends. On failure, the PAM handle
is cleaned up internally; do not call auth_close_session.
Returns PAM_SUCCESS (0) on success, or a nonzero error code on failure */
int auth_open_session(auth_result *result);

/* Abort after auth_authenticate: clean up the PAM handle without opening a
session. Use when the login is rejected after Phase 1. */
void auth_cancel(auth_result *result);

/* Close an open session and free all resources. */
void auth_close_session(auth_result *result);
