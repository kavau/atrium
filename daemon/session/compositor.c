#include "compositor.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "auth.h"
#include "daemon/policy/config.h"
#include "exec.h"
#include "lib/log.h"
#include "sessions.h"

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

    /* Resolve compositor command and desktop names. Configured compositor
    override has priority over user-selected sessions.

    XDG_CURRENT_DESKTOP is set from DesktopNames (e.g. "KDE");
    XDG_SESSION_DESKTOP and DESKTOP_SESSION are set from the desktop entry's
    basename (e.g. "plasma"). A compositor override has no desktop entry, so
    both are set from `desktop`. */
    const char *compositor_cmd = config_compositor();
    const char *current_desktop = config_desktop();
    const char *session_desktop = config_desktop();
    log_debug("child_exec_compositor: config compositor '%s', desktop '%s'", compositor_cmd,
              current_desktop);
    if (!compositor_cmd || !*compositor_cmd) {
        log_debug("child_exec_compositor: no compositor override, looking up session '%s'",
                  session_id);
        const session_entry *e = (*session_id) ? sessions_find(session_id) : NULL;
        if (e) {
            compositor_cmd = e->exec;
            current_desktop = e->desktop_names ? e->desktop_names : e->id;
            session_desktop = e->id;
            log_info("child_exec_compositor: session '%s' (%s)", e->id, e->name);
        } else if (*session_id) {
            log_warn("child_exec_compositor: session '%s' not found", session_id);
        }
    }
    if (!compositor_cmd || !*compositor_cmd) {
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
