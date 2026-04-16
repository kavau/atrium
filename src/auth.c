/*
 * auth.c — PAM (Pluggable Authentication Modules) authentication for atrium.
 *
 * Implements auth_begin(), auth_open_session(), and auth_close() declared in
 * auth.h.
 *
 * The PAM sequence is split across two processes:
 *
 *   Daemon (parent):  auth_begin()
 *     pam_authenticate  — password check (delegates to auth modules)
 *     pam_acct_mgmt     — account validity (expiry, lockout, time restrictions)
 *     getpwnam          — resolve uid and gid from the username
 *
 *   Compositor child:  auth_open_session()  [called after fork, before exec]
 *     pam_setcred       — establish user credentials (Kerberos tickets, etc.)
 *     pam_open_session  — session setup (audit log, loginuid, keyring, limits)
 *     pam_getenvlist    — collect environment variables set by PAM modules
 *
 * This split is necessary because pam_open_session triggers pam_loginuid.so
 * (which writes /proc/self/loginuid — a one-shot value) and other session
 * modules that must run in the compositor's PID context, not the daemon's.
 *
 * auth_close() runs in the daemon after the compositor exits.
 *
 * GNU extensions used:
 *   strdup(3)    — POSIX.1-2008, exposed via _GNU_SOURCE
 * _GNU_SOURCE is injected by the build system; do not redefine it here.
 */

#include "auth.h"
#include "log.h"

#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

#include <security/pam_appl.h>

/*
 * pam_conv_fn — PAM conversation callback.
 *
 * appdata_ptr is the password (const char *), cast to void * by the caller to
 * satisfy the PAM API.  PAM calls this function during pam_authenticate() to
 * collect credentials or deliver messages from the underlying modules.
 *
 * Message types we handle:
 *   PAM_PROMPT_ECHO_OFF — password entry: return a malloc'd copy of the password
 *   PAM_PROMPT_ECHO_ON  — username entry (rarely sent; pam_start already sets
 *                         the user): return an empty string
 *   PAM_ERROR_MSG       — error text from a PAM module: log it
 *   PAM_TEXT_INFO       — informational text from a PAM module: log it
 *
 * PAM takes ownership of the pam_response array and all resp strings inside
 * it, so we malloc them.  On allocation failure we free what we have so far
 * and return PAM_BUF_ERR; PAM is then responsible for freeing the partial
 * array (Linux-PAM does this correctly).
 */
static int pam_conv_fn(int num_msg, const struct pam_message **msg,
                       struct pam_response **resp, void *appdata_ptr)
{
    const char *password = appdata_ptr;

    struct pam_response *r = calloc(num_msg, sizeof(*r));
    if (!r)
        return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
            r[i].resp = strdup(password ? password : "");
            if (!r[i].resp)
                goto oom;
            break;

        case PAM_PROMPT_ECHO_ON:
            r[i].resp = strdup("");
            if (!r[i].resp)
                goto oom;
            break;

        case PAM_ERROR_MSG:
            log_error("PAM: %s", msg[i]->msg);
            break;

        case PAM_TEXT_INFO:
            log_info("PAM: %s", msg[i]->msg);
            break;
        }
        /* resp_retcode is defined as unused by Linux-PAM; must be zero. */
        r[i].resp_retcode = 0;
    }

    *resp = r;
    return PAM_SUCCESS;

oom:
    log_error("pam_conv_fn: out of memory allocating response (num_msg=%d)", num_msg);
    for (int j = 0; j < num_msg; j++)
        free(r[j].resp);
    free(r);
    return PAM_BUF_ERR;
}

int auth_begin(const char *username, const char *password, auth_result *out)
{
    assert(username);
    assert(password);
    assert(out);

    /*
     * Cast away const on password: the PAM conv API uses void * for appdata.
     * pam_conv_fn only reads it as const char *, so this is safe.
     */
    struct pam_conv conv = {
        .conv        = pam_conv_fn,
        .appdata_ptr = (void *)password,
    };

    pam_handle_t *pamh;
    int r = pam_start("atrium", username, &conv, &pamh);
    if (r != PAM_SUCCESS) {
        /* pamh is invalid on pam_start failure; pass NULL to pam_strerror. */
        log_error("pam_start: %s", pam_strerror(NULL, r));
        return r;
    }

    /*
     * Suppress the per-failure delay that pam_unix inserts to slow brute-force
     * attacks.  The display manager serialises retries through the greeter UI
     * anyway, so the kernel-level delay only harms user experience here.
     */
    pam_fail_delay(pamh, 0);

    r = pam_authenticate(pamh, 0);
    if (r != PAM_SUCCESS) {
        log_error("pam_authenticate: %s", pam_strerror(pamh, r));
        pam_end(pamh, r);
        return r;
    }

    r = pam_acct_mgmt(pamh, 0);
    if (r != PAM_SUCCESS) {
        log_error("pam_acct_mgmt: %s", pam_strerror(pamh, r));
        pam_end(pamh, r);
        return r;
    }

    /*
     * Resolve uid/gid while we still have valid user context.  getpwnam()
     * returns NULL both on hard error (errno set) and for an unknown user
     * (errno left at 0), so we clear errno first to distinguish the two.
     */
    errno = 0;
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        log_error("getpwnam(%s): %s", username,
                  errno ? strerror(errno) : "user not found");
        pam_end(pamh, PAM_SYSTEM_ERR);
        return PAM_SYSTEM_ERR;
    }

    out->uid     = pw->pw_uid;
    out->gid     = pw->pw_gid;
    out->pam_env = NULL;
    out->pamh    = pamh;

    log_info("auth: %s authenticated, uid=%u gid=%u", username,
             (unsigned)out->uid, (unsigned)out->gid);
    return PAM_SUCCESS;
}

int auth_open_session(auth_result *result)
{
    assert(result);
    assert(result->pamh);

    pam_handle_t *pamh = result->pamh;

    int r = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    if (r != PAM_SUCCESS) {
        log_error("pam_setcred: %s", pam_strerror(pamh, r));
        return r;
    }

    r = pam_open_session(pamh, 0);
    if (r != PAM_SUCCESS) {
        log_error("pam_open_session: %s", pam_strerror(pamh, r));
        pam_setcred(pamh, PAM_DELETE_CRED);
        return r;
    }

    result->pam_env = pam_getenvlist(pamh);
    return PAM_SUCCESS;
}

void auth_close(auth_result *result)
{
    assert(result);
    assert(result->pamh);

    pam_close_session(result->pamh, 0);
    pam_setcred(result->pamh, PAM_DELETE_CRED);
    pam_end(result->pamh, PAM_SUCCESS);
    result->pamh = NULL;

    /*
     * pam_getenvlist() transfers ownership to the caller: we must free each
     * string and the array itself.  Do this after pam_end() — the two
     * allocations are independent.
     */
    if (result->pam_env) {
        for (char **e = result->pam_env; *e; e++)
            free(*e);
        free(result->pam_env);
        result->pam_env = NULL;
    }
}
