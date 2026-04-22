#include "auth.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LEN_SEAT 64

/* PAM conversation callback */
static int conv_callback(int num_msg, const struct pam_message **msg, struct pam_response **resp,
                         void *appdata_ptr) {
    const char *password = (const char *)appdata_ptr;

    struct pam_response *responses = calloc(num_msg, sizeof(*responses));
    if (!responses) {
        fprintf(stderr, "conv_callback: failed to allocate responses\n");
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
                fprintf(stderr, "PAM message: %s\n", msg[i]->msg);
                break;
            default:
                fprintf(stderr, "conv_callback: unsupported message style %d\n", msg[i]->msg_style);
                r = PAM_CONV_ERR;
                goto err;
        }
        responses[i].resp_retcode = 0; /* unused, must be zero */
    }

    *resp = responses;
    return PAM_SUCCESS;

oom:
    fprintf(stderr, "conv_callback: out of memory\n");
    r = PAM_BUF_ERR;

err:
    for (int j = 0; j < num_msg; j++) {
        free(responses[j].resp);
    }
    free(responses);
    return r;
}

int auth_open_session(const char *username, const char *password, const char *seat,
                      const char *conf_path, auth_result *result) {
    assert(username);
    assert(password);
    assert(seat);
    assert(result);

    struct pam_conv conv = {
        .conv = conv_callback,
        .appdata_ptr = (void *)password,
    };

    pam_handle_t *pamh;

    /* pam_start_confdir() is equivalent to pam_start() but allows setting a
     * configuration directory other than /etc/pam.d - useful for testing. */
    int r = pam_start_confdir("atrium", username, &conv, conf_path, &pamh);
    if (r != PAM_SUCCESS) {
        /* pamh is invalid on pam_start failure; pass NULL instead */
        fprintf(stderr, "pam_start failed: %s\n", pam_strerror(NULL, r));
        return r;
    }

    /* Set up the PAM environment so PAM knows for which seat to open the session */
    char seat_env[MAX_LEN_SEAT + 1];
    snprintf(seat_env, sizeof(seat_env), "XDG_SEAT=%s", seat);
    pam_putenv(pamh, seat_env);

    r = pam_authenticate(pamh, 0); /* authenticate the user */
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "pam_authenticate failed: %s\n", pam_strerror(pamh, r));
        pam_end(pamh, r);
        return r;
    }

    r = pam_acct_mgmt(pamh, 0); /* check if account is valid */
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "pam_acct_mgmt failed: %s\n", pam_strerror(pamh, r));
        pam_end(pamh, r);
        return r;
    }

    fprintf(stderr, "PAM authentication successful\n");

    r = pam_setcred(pamh, PAM_ESTABLISH_CRED); /* set user credentials */
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "pam_setcred failed: %s\n", pam_strerror(pamh, r));
        pam_end(pamh, r);
        return r;
    }

    r = pam_open_session(pamh, 0); /* establish the user session */
    if (r != PAM_SUCCESS) {
        fprintf(stderr, "pam_open_session failed: %s\n", pam_strerror(pamh, r));
        pam_setcred(pamh, PAM_DELETE_CRED);
        pam_end(pamh, r);
        return r;
    }

    result->env = pam_getenvlist(pamh);
    result->pam_handle = pamh;
    return PAM_SUCCESS;
}

void auth_close_session(auth_result *result) {
    assert(result);
    assert(result->pam_handle);

    pam_close_session(result->pam_handle, 0);
    pam_setcred(result->pam_handle, PAM_DELETE_CRED);
    pam_end(result->pam_handle, PAM_SUCCESS);
    result->pam_handle = NULL;

    /* Free the resources returned by pam_getenvlist() */
    for (char **p = result->env; p && *p; p++) {
        free(*p);
    }
    free(result->env);
    result->env = NULL;
}
