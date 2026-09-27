#include "exec.h"

#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#ifdef HAVE_SD_JOURNAL
#include <systemd/sd-journal.h>
#endif
#include <unistd.h>

#include "lib/conf_helpers.h"
#include "lib/log.h"

/* File containing login environment definitions. */
#define LOGIN_DEFS_PATH "/etc/login.defs"

/* Used when login.defs has nothing to say about the PATH. */
#define FALLBACK_PATH_ENV "PATH=/usr/local/bin:/usr/bin:/bin"

/* Return the login PATH declared in login.defs (ENV_SUPATH for uid 0, ENV_PATH
otherwis) as an allocated "PATH=..." string. Returns NULL when the file or the
key is absent. */
static char *login_defs_path(uid_t uid) {
    char *val = conf_file_lookup(LOGIN_DEFS_PATH, (uid == 0) ? "ENV_SUPATH" : "ENV_PATH");
    if (!val)
        return NULL;

    /* login.defs conventionally declares the whole assignment, e.g.
    "ENV_PATH PATH=/usr/bin", but we should tolerate a value without prefix. */
    if (strncmp(val, "PATH=", 5) == 0)
        return val;

    char *path_env = NULL;
    if (asprintf(&path_env, "PATH=%s", val) < 0) {
        log_error("login_defs_path: out of memory");
        path_env = NULL;
    }
    free(val);
    return path_env;
}

int env_append_passwd(const char *username, char **env, int i, struct passwd **pw_out) {
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        log_error("env_append_passwd: getpwnam failed for '%s'", username);
        return -1;
    }
    if (asprintf(&env[i++], "USER=%s", pw->pw_name) < 0)
        goto oom;
    if (asprintf(&env[i++], "LOGNAME=%s", pw->pw_name) < 0)
        goto oom;
    if (asprintf(&env[i++], "HOME=%s", pw->pw_dir) < 0)
        goto oom;
    if (asprintf(&env[i++], "SHELL=%s", pw->pw_shell) < 0)
        goto oom;
    char *path_env = login_defs_path(pw->pw_uid);
    if (path_env) {
        log_debug("env_append_passwd: %s from %s", path_env, LOGIN_DEFS_PATH);
        env[i++] = path_env;
    } else {
        log_debug("env_append_passwd: using built-in %s", FALLBACK_PATH_ENV);
        env[i++] = FALLBACK_PATH_ENV;
    }
    *pw_out = pw;
    return i;
oom:
    log_error("env_append_passwd: out of memory");
    return -1;
}

/* Redirect stderr to the systemd journal under "atrium" so that child process
output (cage, greeter, compositor) appears alongside daemon log lines. */
static void redirect_stderr_to_journal(void) {
    /* Skip redirect if ATRIUM_LOG_STDERR is set, so child output goes to the
    terminal instead of the journal when testing interactively. */
    if (getenv("ATRIUM_LOG_STDERR"))
        return;
#ifdef HAVE_SD_JOURNAL
    int jfd = sd_journal_stream_fd("atrium", LOG_DEBUG, 1);
    if (jfd >= 0) {
        dup2(jfd, STDERR_FILENO);
        close(jfd);
    }
#endif
}

_Noreturn void drop_privs_and_run(struct passwd *pw, const char *cmd, char *const env[],
                                  const char *wrapper) {
    /* Run the wrapper as "sh <wrapper> <cmd>", if it exists. */
    if (wrapper && *wrapper) {
        if (access(wrapper, F_OK) == 0) {
            char *argv[] = {"sh", (char *)wrapper, (char *)cmd, NULL};
            drop_privs_and_exec(pw, "/bin/sh", argv, env);
        }
        log_warn("drop_privs_and_run: session wrapper '%s' not found, running '%s' directly",
                 wrapper, cmd);
    }

    /* Otherwise fall back to executing the command directly.
    sh is non-interactive and non-login. "exec" replaces sh with the target
    process so the child PID is the real process and SIGTERM reaches it
    directly. SHORTCUT: sh interprets shell metacharacters in cmd, which is an
    acceptable violation of the Desktop Entry spec. */
    char *exec_str;
    if (asprintf(&exec_str, "exec %s", cmd) < 0) {
        log_error("drop_privs_and_run: out of memory");
        _exit(EXIT_FAILURE);
    }
    char *argv[] = {"sh", "-c", exec_str, NULL};
    drop_privs_and_exec(pw, "/bin/sh", argv, env);
}

_Noreturn void drop_privs_and_exec(struct passwd *pw, const char *exe, char *const argv[],
                                   char *const env[]) {
    /* Privilege drop order: supplementary groups -> gid -> uid.
    setresgid/setresuid set all three ID slots (real, effective, saved)
    atomically. */
    if (initgroups(pw->pw_name, pw->pw_gid) < 0) {
        log_syserr("drop_privs_and_exec: initgroups");
        _exit(EXIT_FAILURE);
    }
    if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0) {
        log_syserr("drop_privs_and_exec: setresgid");
        _exit(EXIT_FAILURE);
    }
    if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
        log_syserr("drop_privs_and_exec: setresuid");
        _exit(EXIT_FAILURE);
    }

    /* Defence-in-depth: verify we cannot re-escalate to root. */
    if (setresuid(0, 0, 0) == 0) {
        log_error("CRITICAL: re-escalation to root succeeded after privilege drop");
        _exit(EXIT_FAILURE);
    }

    if (chdir(pw->pw_dir) < 0)
        log_syserr("drop_privs_and_exec: chdir"); /* not fatal */

    redirect_stderr_to_journal();

    /* Reset SIGPIPE so the child gets the default disposition. */
    signal(SIGPIPE, SIG_DFL);

    execvpe(exe, argv, env);
    log_syserr("drop_privs_and_exec: execvpe");
    _exit(EXIT_FAILURE);
}
