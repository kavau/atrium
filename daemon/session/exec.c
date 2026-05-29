#include "exec.h"

#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <systemd/sd-journal.h>
#include <unistd.h>

#include "lib/log.h"

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
    env[i++] = "PATH=/usr/local/bin:/usr/bin:/bin";
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
    int jfd = sd_journal_stream_fd("atrium", LOG_DEBUG, 0);
    if (jfd >= 0) {
        dup2(jfd, STDERR_FILENO);
        close(jfd);
    }
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

    execvpe(exe, argv, env);
    log_syserr("drop_privs_and_exec: execvpe");
    _exit(EXIT_FAILURE);
}
