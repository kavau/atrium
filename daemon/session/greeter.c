#include "greeter.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "daemon/core/seat.h"
#include "daemon/policy/config.h"
#include "exec.h"
#include "lib/ipc.h"
#include "lib/log.h"

_Noreturn void child_exec_greeter(const char *username, const seat *s, ipc_channel *ch,
                                  const char *session_list, const char *preselect) {
    assert(username);
    assert(s);
    assert(session_list);
    assert(preselect);

    /* Block until parent sends session_id. EOF (n <= 0) means parent failed
    (e.g. CreateSession error). */
    char session_id[32] = {0};
    ssize_t n = ipc_recv(ch, session_id, sizeof(session_id) - 1);
    if (n <= 0) {
        log_error("child_exec_greeter: failed to receive session_id from parent");
        _exit(EXIT_FAILURE);
    }

    /* Build the session environment - count the number of env vars first. */
    int n_env = 0;
    char **ipc_env = ipc_getenvlist(ch);
    if (!ipc_env)
        goto oom;
    for (char **p = ipc_env; *p; p++)
        ++n_env;
    n_env += NUM_ENV_PASSWD;
    /* XDG_SEAT [XDG_VTNR] XDG_SESSION_TYPE XDG_SESSION_CLASS XDG_RUNTIME_DIR
    XDG_SESSION_ID WLR_LIBINPUT_NO_DEVICES [ATRIUM_SESSION_LIST]
    [ATRIUM_SESSION_PRESELECT] NULL */
    n_env += 7 + (s->vtnr > 0 ? 1 : 0) + (*session_list ? 1 : 0) + (*preselect ? 1 : 0);

    char **env = calloc(n_env, sizeof(*env));
    if (!env) {
        log_syserr("child_exec_greeter: calloc");
        _exit(EXIT_FAILURE);
    }

    struct passwd *pw;
    int i = 0;
    i = env_append_passwd(username, env, i, &pw);
    if (i < 0)
        _exit(EXIT_FAILURE); /* logged by helper */

    if (asprintf(&env[i++], "XDG_SEAT=%s", s->name) < 0)
        goto oom;
    if (s->vtnr > 0 && asprintf(&env[i++], "XDG_VTNR=%d", s->vtnr) < 0)
        goto oom;
    env[i++] = "XDG_SESSION_TYPE=wayland";
    env[i++] = "XDG_SESSION_CLASS=greeter";
    if (asprintf(&env[i++], "XDG_RUNTIME_DIR=/run/user/%u", (unsigned)pw->pw_uid) < 0)
        goto oom;
    if (asprintf(&env[i++], "XDG_SESSION_ID=%s", session_id) < 0)
        goto oom;
    env[i++] = "WLR_LIBINPUT_NO_DEVICES=1";
    if (*session_list && asprintf(&env[i++], "ATRIUM_SESSION_LIST=%s", session_list) < 0)
        goto oom;
    if (*preselect && asprintf(&env[i++], "ATRIUM_SESSION_PRESELECT=%s", preselect) < 0)
        goto oom;

    for (char **p = ipc_env; *p; p++)
        env[i++] = *p;

    env[i++] = NULL;
    assert(i == n_env);

    if (ipc_prepare_for_exec(ch) < 0) {
        log_syserr("child_exec_greeter: ipc_prepare_for_exec");
        _exit(EXIT_FAILURE);
    }

    log_debug("child_exec_greeter: exec: %s", config_greeter());
    drop_privs_and_run(pw, config_greeter(), env);

oom:
    log_error("child_exec_greeter: out of memory");
    _exit(EXIT_FAILURE); /* no cleanup needed */
}
