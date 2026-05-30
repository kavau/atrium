#include "compositor.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "auth.h"
#include "daemon/core/config.h"
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

    /* Resolve compositor command and desktop name from the chosen session. */
    const char *compositor_cmd  = config_compositor();
    const char *desktop         = config_desktop();
    const session_entry *e = (*session_id) ? sessions_find(session_id) : NULL;
    if (e) {
        compositor_cmd = e->exec;
        desktop  = e->desktop_names ? e->desktop_names : e->id;
        log_info("child_exec_compositor: session '%s' (%s)", e->id, e->name);
    } else if (*session_id) {
        log_warn("child_exec_compositor: session '%s' not found, falling back to config",
                 session_id);
    }

    if (!compositor_cmd || !*compositor_cmd) {
        log_error("child_exec_compositor: no compositor configured");
        _exit(EXIT_FAILURE);
    }

    /* Build the session environment: count PAM env entries. */
    int n_pam = 0;
    for (char **p = pam_result->env; p && *p; p++)
        n_pam++;

    /* passwd fields + PAM env entries + DBUS + XDG desktop (x2) + NULL */
    int n_env = 5 + n_pam + 4;
    char **env = calloc(n_env, sizeof(*env));
    if (!env) {
        log_syserr("child_exec_compositor: calloc");
        _exit(EXIT_FAILURE);
    }

    struct passwd *pw;
    int i = 0;
    /* Passwd fields come first so they are authoritative over PAM duplicates. */
    i = env_append_passwd(username, env, i, &pw);
    if (i < 0)
        _exit(EXIT_FAILURE); /* logged by helper */
    for (char **p = pam_result->env; p && *p; p++)
        env[i++] = *p;
    if (asprintf(&env[i++], "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/%u/bus",
                 (unsigned)pw->pw_uid) < 0)
        goto oom;
    if (asprintf(&env[i++], "XDG_SESSION_DESKTOP=%s", desktop) < 0)
        goto oom;
    if (asprintf(&env[i++], "XDG_CURRENT_DESKTOP=%s", desktop) < 0)
        goto oom;
    env[i++] = NULL;
    assert(i == n_env);

    /* Build "exec <compositor>" for the login shell to run. */
    char *exec_str;
    if (asprintf(&exec_str, "exec %s", compositor_cmd) < 0)
        goto oom;

    /* Prepend '-' to argv[0] to force a login shell (reads .profile, sets PATH). */
    char argv0[64];
    const char *base = strrchr(pw->pw_shell, '/');
    snprintf(argv0, sizeof(argv0), "-%s", base ? base + 1 : pw->pw_shell);
    char *argv[] = {argv0, "-c", exec_str, NULL};

    log_debug("child_exec_compositor: exec '%s' for user '%s'", compositor_cmd, username);
    drop_privs_and_exec(pw, pw->pw_shell, argv, env);

oom:
    log_error("child_exec_compositor: out of memory");
    _exit(EXIT_FAILURE); /* no cleanup needed */
}
