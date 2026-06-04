#include "auth.h"

#include <assert.h>
#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>

#include "lib/log.h"

/* PAM conversation callback */
static int conv_callback(int num_msg, const struct pam_message **msg, struct pam_response **resp,
                         void *appdata_ptr) {
    const char *password = (const char *)appdata_ptr;

    struct pam_response *responses = calloc(num_msg, sizeof(*responses));
    if (!responses) {
        log_error("conv_callback: failed to allocate responses");
        return PAM_BUF_ERR;
    }

    int r = PAM_SUCCESS;
    for (int i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
                /* Password prompt - return a copy of the password from appdata_ptr. */
                responses[i].resp = strdup(password);
                if (!responses[i].resp) {
                    goto oom;
                }
                break;
            case PAM_PROMPT_ECHO_ON:
                /* Username prompt - unused since we're passing the username directly. */
                responses[i].resp = strdup("");
                if (!responses[i].resp) {
                    goto oom;
                }
                break;
            case PAM_ERROR_MSG:
            case PAM_TEXT_INFO:
                /* Display message - just print it to stderr. */
                log_info("PAM message: %s", msg[i]->msg);
                break;
            default:
                log_error("PAM: unsupported message style %d", msg[i]->msg_style);
                r = PAM_CONV_ERR;
                goto err;
        }
        responses[i].resp_retcode = 0; /* unused, must be zero */
    }

    *resp = responses;
    return PAM_SUCCESS;

oom:
    log_error("PAM conv_callback: out of memory");
    r = PAM_BUF_ERR;

err:
    for (int j = 0; j < num_msg; j++) {
        free(responses[j].resp);
    }
    free(responses);
    return r;
}

int auth_authenticate(const char *username, const char *password, const char **env,
                      const char *pam_conf_path, const char *service_name, auth_result *result) {
    assert(username);
    assert(password);
    assert(result);

    struct pam_conv conv = {
        .conv = conv_callback,
        .appdata_ptr = (void *)password,
    };

    pam_handle_t *pamh;

#ifdef HAVE_PAM_START_CONFDIR
    /* pam_start_confdir() is equivalent to pam_start() but allows setting a
     * configuration directory other than /etc/pam.d - useful for testing. */
    log_info("starting PAM auth with config path %s",
             pam_conf_path ? pam_conf_path : "(default)");
    int r = pam_start_confdir(service_name, username, &conv, pam_conf_path, &pamh);
#else
    if (pam_conf_path)
        log_warn("auth_authenticate: ignoring pam_conf_path %s", pam_conf_path);
    int r = pam_start(service_name, username, &conv, &pamh);
#endif
    if (r != PAM_SUCCESS) {
        /* pamh is invalid on pam_start failure; pass NULL instead */
        log_error("pam_start failed: %s", pam_strerror(NULL, r));
        return r;
    }

    /* Set up the PAM environment so PAM knows for which seat to open the session */
    for (const char **p = env; p && *p; p++)
        pam_putenv(pamh, *p);

    r = pam_authenticate(pamh, 0);
    if (r != PAM_SUCCESS) {
        log_error("pam_authenticate failed: %s", pam_strerror(pamh, r));
        pam_end(pamh, r);
        return r;
    }

    r = pam_acct_mgmt(pamh, 0);
    if (r != PAM_SUCCESS) {
        log_error("pam_acct_mgmt failed: %s", pam_strerror(pamh, r));
        pam_end(pamh, r);
        return r;
    }
    log_info("PAM authentication successful");

    result->pam_handle = pamh;
    result->env = NULL;
    return PAM_SUCCESS;
}

int auth_open_session(auth_result *result) {
    assert(result);
    assert(result->pam_handle);

    pam_handle_t *pamh = result->pam_handle;

    int r = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    if (r != PAM_SUCCESS) {
        log_error("pam_setcred failed: %s", pam_strerror(pamh, r));
        pam_end(pamh, r);
        result->pam_handle = NULL;
        return r;
    }

    r = pam_open_session(pamh, 0);
    if (r != PAM_SUCCESS) {
        log_error("pam_open_session failed: %s", pam_strerror(pamh, r));
        pam_setcred(pamh, PAM_DELETE_CRED);
        pam_end(pamh, r);
        result->pam_handle = NULL;
        return r;
    }
    log_info("PAM session started successfully");

    result->env = pam_getenvlist(pamh);
    return PAM_SUCCESS;
}

void auth_cancel(auth_result *result) {
    assert(result);
    assert(result->pam_handle);
    pam_end(result->pam_handle, PAM_SUCCESS);
    result->pam_handle = NULL;
}

void auth_close_session(auth_result *result) {
    assert(result);
    assert(result->pam_handle);

    pam_close_session(result->pam_handle, 0);
    pam_setcred(result->pam_handle, PAM_DELETE_CRED);
    pam_end(result->pam_handle, PAM_SUCCESS);
    result->pam_handle = NULL;
    log_debug("PAM session closed");

    /* Free the resources returned by pam_getenvlist() */
    for (char **p = result->env; p && *p; p++) {
        free(*p);
    }
    free(result->env);
    result->env = NULL;
}
