#include "compositor.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "auth.h"
#include "daemon/policy/config.h"
#include "exec.h"
#include "lib/log.h"
#include "sessions.h"

session_choice session_resolve(const char *session_id) {
    assert(session_id);

    const char *cmd = config_compositor();
    const char *desktop = config_desktop();

    session_choice c = {
        .compositor_cmd = cmd ? cmd : "",
        .current_desktop = desktop ? desktop : "",
        .session_desktop = desktop ? desktop : "",
        .entry = NULL,
    };

    if (*c.compositor_cmd)
        return c; /* configured compositor override wins */

    c.entry = (*session_id) ? sessions_find(session_id) : NULL;
    if (c.entry) {
        c.compositor_cmd = c.entry->exec;
        c.current_desktop = c.entry->desktop_names ? c.entry->desktop_names : c.entry->id;
        c.session_desktop = c.entry->id;
    }
    return c;
}

_Noreturn void child_exec_compositor(const char *username, const auth_result *pam_result,
                                     const char *session_id) {
    assert(username);
    assert(pam_result);
    assert(pam_result->pam_handle);
    assert(session_id);

#ifdef ATRIUM_DEBUG
    if (pam_result->env) {
        log_debug("PAM environment variables:");
        for (char **p = pam_result->env; *p; p++)
            log_debug("  %s", *p);
    }
#endif

    session_choice sel = session_resolve(session_id);
    const char    *compositor_cmd = sel.compositor_cmd;
    const char    *current_desktop = sel.current_desktop;
    const char    *session_desktop = sel.session_desktop;

    if (sel.entry)
        log_info("child_exec_compositor: session '%s' (%s)", sel.entry->id, sel.entry->name);
    else if (*compositor_cmd)
        log_debug("child_exec_compositor: compositor override '%s', desktop '%s'", compositor_cmd,
                  current_desktop);
    else if (*session_id)
        log_warn("child_exec_compositor: session '%s' not found", session_id);

    if (!*compositor_cmd) {
        log_error("child_exec_compositor: no compositor configured");
        _exit(EXIT_FAILURE);
    }

    /* Build the session environment: count PAM env entries. */
    int n_pam = 0;
    for (char **p = pam_result->env; p && *p; p++)
        n_pam++;

    /* passwd fields + PAM env entries + DBUS + XDG_DATA_DIRS + desktop names
    + NULL. Unknown names are left unset rather than exported empty. */
    int    n_desktop = (*current_desktop ? 1 : 0) + (*session_desktop ? 2 : 0);
    int    n_env = NUM_ENV_PASSWD + n_pam + 2 + n_desktop + 1;
    char **env = calloc(n_env, sizeof(*env));
    if (!env) {
        log_syserr("child_exec_compositor: calloc");
        _exit(EXIT_FAILURE);
    }

    struct passwd *pw;
    int            i = 0;
    /* Passwd fields come first so they are authoritative over PAM duplicates. */
    i = env_append_passwd(username, env, i, &pw);
    if (i < 0)
        _exit(EXIT_FAILURE); /* logged by helper */
    for (char **p = pam_result->env; p && *p; p++)
        env[i++] = *p;
    if (asprintf(&env[i++], "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/%u/bus",
                 (unsigned)pw->pw_uid) < 0)
        goto oom;
    /* Spec default, added after the PAM entries so a PAM-supplied value wins. */
    env[i++] = "XDG_DATA_DIRS=/usr/local/share:/usr/share";
    /* XDG_CURRENT_DESKTOP is set from DesktopNames (e.g. "KDE");
    XDG_SESSION_DESKTOP and DESKTOP_SESSION are set from the desktop entry's
    basename (e.g. "plasma"). */
    if (*current_desktop && asprintf(&env[i++], "XDG_CURRENT_DESKTOP=%s", current_desktop) < 0)
        goto oom;
    if (*session_desktop) {
        if (asprintf(&env[i++], "XDG_SESSION_DESKTOP=%s", session_desktop) < 0)
            goto oom;
        if (asprintf(&env[i++], "DESKTOP_SESSION=%s", session_desktop) < 0)
            goto oom;
    }
    env[i++] = NULL;
    assert(i == n_env);

    log_debug("child_exec_compositor: exec '%s' for user '%s'", compositor_cmd, username);
    for (int j = 0; env[j]; j++)
        log_debug("  env[%d]: %s", j, env[j]);
    drop_privs_and_run(pw, compositor_cmd, env, config_session_wrapper());

oom:
    log_error("child_exec_compositor: out of memory");
    _exit(EXIT_FAILURE); /* no cleanup needed */
}
