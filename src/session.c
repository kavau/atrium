#include "session.h"
#include "bus.h"
#include "config.h"
#include "log.h"

#include <systemd/sd-journal.h>
#include <systemd/sd-login.h>
#include <syslog.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Poll sd_session_is_active() until logind marks the session active.
 * This reads /run/systemd/sessions/<id> directly — no D-Bus round-trip.
 * Returns 0 on success, -1 on error or timeout. */
static int wait_session_active(const char *session_id)
{
    const int MAX_POLLS = 100;   /* 100 × 20 ms = 2 s ceiling */
    const int POLL_US   = 20000; /* 20 ms */

    for (int i = 0; i < MAX_POLLS; i++) {
        int r = sd_session_is_active(session_id);
        if (r > 0) {
            log_info("session %s active (waited %d ms)", session_id, i * 20);
            return 0;
        }
        if (r < 0) {
            log_error("sd_session_is_active(%s): %s", session_id, strerror(-r));
            return -1;
        }
        usleep(POLL_US);
    }

    log_error("session %s: timed out waiting for active state (2 s)", session_id);
    return -1;
}

/* Wait for the udev event queue to drain so device ACLs are applied before
 * the compositor opens DRM/input devices.  Forks udevadm as a subprocess
 * to avoid blocking the daemon's event loop for long. */
static void wait_udev_settle(void)
{
    pid_t settle_pid = fork();
    if (settle_pid == 0) {
        execl("/usr/bin/udevadm", "udevadm", "settle", "--timeout=5",
              (char *)NULL);
        _exit(127);
    } else if (settle_pid > 0) {
        int settle_st;
        waitpid(settle_pid, &settle_st, 0);
        if (!WIFEXITED(settle_st) || WEXITSTATUS(settle_st) != 0)
            log_warn("udevadm settle exited with status %d",
                     WEXITSTATUS(settle_st));
        else
            log_debug("udevadm settle complete");
    } else {
        log_warn("fork for udevadm settle failed: %s", strerror(errno));
    }
}

/* Configure the environment, drop privileges, and exec the child process.
 * When is_greeter is set, execs cage hosting atrium-greeter.  Otherwise
 * execs the compositor via the user's login shell.
 * Called from the child side of the fork after the sync pipe has signalled
 * that the parent finished setting up the logind session.
 * This function never returns. */
static _Noreturn void child_exec(const struct seat *s,
                                 const struct passwd *pw,
                                 const char *runtime_dir,
                                 int is_greeter)
{
    log_debug("%s: child preparing %s (uid=%u gid=%u)",
              s->name, is_greeter ? "greeter" : "compositor",
              (unsigned)pw->pw_uid, (unsigned)pw->pw_gid);

    /* Redirect stderr to the journal so that child output (cage, greeter,
     * compositor) appears under the "atrium" syslog identifier alongside
     * daemon messages, regardless of cgroup placement. */
    int jfd = sd_journal_stream_fd("atrium", LOG_DEBUG, 0);
    if (jfd >= 0) {
        dup2(jfd, STDERR_FILENO);
        close(jfd);
    }

    /* Unblock the signals the parent masked for signalfd (SIGTERM, SIGCHLD).
     * fork() inherits the signal mask; exec() does NOT reset it.  Without
     * this the compositor would run with those signals blocked, preventing
     * it from receiving SIGTERM and interfering with its own child-reaping.
     * We use SIG_UNBLOCK rather than SIG_SETMASK so we only undo what the
     * parent explicitly blocked, preserving any other mask state. */
    sigset_t parent_mask;
    sigemptyset(&parent_mask);
    sigaddset(&parent_mask, SIGTERM);
    sigaddset(&parent_mask, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &parent_mask, NULL);

    /* Clear display environment variables inherited from the parent.
     * When running as a systemd service these are never set, so these
     * calls are no-ops in production. When testing from a graphical
     * terminal they prevent the compositor from trying to connect to
     * the existing session's display. */
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");

    /* Set session environment. XDG_RUNTIME_DIR is required by the
     * compositor; the rest is informational. These are all values known
     * before fork, so they are valid in the child's address space. */
    setenv("XDG_SESSION_TYPE",    "wayland",          1);
    setenv("XDG_SESSION_DESKTOP", CONFIG_DESKTOP_NAME, 1);
    setenv("XDG_CURRENT_DESKTOP", CONFIG_DESKTOP_NAME, 1);
    setenv("XDG_SEAT",            s->name,             1);
    setenv("XDG_RUNTIME_DIR",     runtime_dir,         1);
    setenv("HOME",              pw->pw_dir,       1);
    setenv("USER",              pw->pw_name,      1);
    setenv("LOGNAME",           pw->pw_name,      1);
    if (pw->pw_shell && pw->pw_shell[0] != '\0')
        setenv("SHELL",         pw->pw_shell,     1);

    /* Set the D-Bus session bus address. On systemd systems the user bus
     * socket lives at /run/user/<uid>/bus.  Most libraries auto-detect this,
     * but some applications check the environment variable explicitly. */
    char dbus_addr[96];
    snprintf(dbus_addr, sizeof(dbus_addr),
             "unix:path=/run/user/%u/bus", (unsigned)pw->pw_uid);
    setenv("DBUS_SESSION_BUS_ADDRESS", dbus_addr, 1);

    if (s->vtnr > 0) {
        char vtnr_str[8];
        snprintf(vtnr_str, sizeof(vtnr_str), "%d", s->vtnr);
        setenv("XDG_VTNR", vtnr_str, 1);
    }

    /* Drop root: supplementary groups, then gid, then uid.
     * setresgid/setresuid set all three ID slots (real, effective, saved)
     * atomically.  Plain setgid/setuid may leave the saved-set-id as root
     * when called from a process with CAP_SETUID/CAP_SETGID. */
    if (initgroups(pw->pw_name, pw->pw_gid) < 0) {
        log_error("initgroups: %s", strerror(errno));
        _exit(1);
    }
    if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0) {
        log_error("setresgid: %s", strerror(errno));
        _exit(1);
    }
    if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
        log_error("setresuid: %s", strerror(errno));
        _exit(1);
    }

    /* Defence-in-depth: verify we cannot re-escalate to root. */
    if (setresuid(0, 0, 0) == 0) {
        log_error("CRITICAL: re-escalation to root succeeded after priv drop");
        _exit(1);
    }

    /* Set the working directory to the user's home. Without this the
     * compositor inherits the daemon's cwd, which is / under systemd. */
    if (chdir(pw->pw_dir) < 0)
        log_warn("chdir(%s): %s", pw->pw_dir, strerror(errno));

    if (is_greeter) {
        /* cage is a kiosk compositor — exec it directly, no login shell.
         * ATRIUM_FULLSCREEN tells the greeter to go fullscreen inside cage
         * rather than using its default windowed mode. */
        setenv("ATRIUM_FULLSCREEN", "1", 1);
        static const char *const greeter_argv[] = CONFIG_GREETER_ARGS;
        log_info("exec greeter: %s", CONFIG_GREETER_CMD);
        execvp(CONFIG_GREETER_CMD, (char *const *)greeter_argv);
        log_error("execvp(%s): %s", CONFIG_GREETER_CMD, strerror(errno));
    } else {
        /* Launch the compositor via the user's login shell.  The -l flag
         * triggers login-shell behaviour, which sources .profile /
         * .bash_profile and sets up the user's PATH and other environment.
         * The 'exec' in the -c string replaces the shell with the compositor
         * so there is no extra process between the daemon and the compositor
         * (SIGTERM reaches it directly, and waitpid sees the compositor's
         * exit status). */
        execlp(pw->pw_shell, pw->pw_shell, "-l", "-c",
               "exec " CONFIG_COMPOSITOR, (char *)NULL);
        log_error("exec %s -l -c 'exec %s': %s",
                  pw->pw_shell, CONFIG_COMPOSITOR, strerror(errno));
    }
    _exit(1);
}

/* Clear session state and close the logind fifo.  Shared by both
 * session_stop() and session_shutdown(), and used in session_start()
 * error paths. */
static void session_cleanup(struct seat *s)
{
    /* Closing the fifo tells logind the session has ended. */
    if (s->session_fifo_fd >= 0) {
        log_debug("closing session fifo fd=%d for seat %s",
                  s->session_fifo_fd, s->name);
        close(s->session_fifo_fd);
        s->session_fifo_fd = -1;
    }

    s->compositor_pid    = 0;
    s->state             = SEAT_IDLE;
    s->session_id[0]     = '\0';
    s->session_object[0] = '\0';
    s->runtime_path[0]   = '\0';
}

static int session_start_impl(struct seat *s, int is_greeter)
{
    const char *label = is_greeter ? "greeter" : "compositor";
    log_debug("starting %s for seat %s", label, s->name);

    /* Resolve the session user.
     * Greeter: fixed uid from config.h (dedicated unprivileged account).
     * Compositor: username received from greeter via credentials pipe. */
    struct passwd pwbuf;
    struct passwd *pw = NULL;
    char pwbuf_data[1024];

    if (is_greeter) {
        int r = getpwuid_r(CONFIG_GREETER_UID, &pwbuf, pwbuf_data,
                           sizeof(pwbuf_data), &pw);
        if (r != 0 || pw == NULL) {
            log_error("%s: getpwuid_r(%d): %s", s->name,
                      CONFIG_GREETER_UID,
                      r != 0 ? strerror(r) : "user not found");
            return -1;
        }
    } else {
        if (s->greeter_username[0] == '\0') {
            log_error("%s: no username (greeter did not send credentials?)",
                      s->name);
            return -1;
        }
        int r = getpwnam_r(s->greeter_username, &pwbuf, pwbuf_data,
                           sizeof(pwbuf_data), &pw);
        if (r != 0 || pw == NULL) {
            log_error("%s: getpwnam_r(%s): %s", s->name,
                      s->greeter_username,
                      r != 0 ? strerror(r) : "user not found");
            return -1;
        }
    }

    log_debug("%s: resolved user %s (uid=%u gid=%u)",
              s->name, pw->pw_name, (unsigned)pw->pw_uid, (unsigned)pw->pw_gid);

    /* Build the XDG_RUNTIME_DIR path before forking so the child can use it
     * directly. The authoritative value comes from CreateSession's reply, but
     * that runs in the parent after fork. Since logind always uses
     * /run/user/<uid>, we can construct it here.
     * SHORTCUT: When PAM-based sessions are added (Phase 9), consider passing
     * session data from parent to child through the sync pipe instead. */
    char runtime_dir[64];
    snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%u",
            (unsigned)pw->pw_uid);

    /* Create a sync pipe to coordinate with the child. The child will block
     * reading from this pipe until the parent has created the logind session
     * and activated it. This ensures the compositor starts in the correct
     * cgroup and with an active session. */
    int sync_pipe[2];
    if (pipe2(sync_pipe, O_CLOEXEC) < 0) {
        log_error("%s: pipe: %s", s->name, strerror(errno));
        return -1;
    }

    /* Fork the compositor. We fork *before* calling CreateSession so we can
     * pass the child's PID to logind. This ensures the session cgroup contains
     * the compositor process, not the display manager. */
    pid_t pid = fork();
    if (pid < 0) {
        log_error("%s: fork: %s", s->name, strerror(errno));
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* ---- child ---- */

        /* Block until the parent signals us by closing the write end of the
         * pipe (which causes read to return 0).  This ensures CreateSession
         * and ActivateSession have completed before we proceed. */
        close(sync_pipe[1]);
        char dummy;
        ssize_t n = read(sync_pipe[0], &dummy, 1);
        close(sync_pipe[0]);
        if (n < 0) {
            log_error("sync pipe read: %s", strerror(errno));
            _exit(1);
        }

        child_exec(s, pw, runtime_dir, is_greeter);
        /* unreachable */
    }

    /* ---- parent ---- */

    /* Close the read end; we only write. */
    close(sync_pipe[0]);

    /* Create a logind session for this seat, passing the child's PID so the
     * session cgroup contains the compositor process. */
    if (bus_create_session(s->name, (uint32_t)s->vtnr, pw->pw_uid, pid,
                           s->session_id,     sizeof(s->session_id),
                           s->session_object, sizeof(s->session_object),
                           s->runtime_path,   sizeof(s->runtime_path),
                           &s->session_fifo_fd) < 0) {
        close(sync_pipe[1]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        session_cleanup(s);  /* clear any partially-written out params */
        return -1;
    }

    log_info("%s: session %s created (runtime %s)",
            s->name, s->session_id, s->runtime_path);

    s->compositor_pid = pid;
    log_debug("forked %s process pid=%d for seat %s", label, pid, s->name);

    /* Activate the session.  For seat0, logind needs an explicit
     * ActivateSession call which triggers an internal chvt.  Other seats
     * become active automatically when they have exactly one session. */
    if (strcmp(s->name, "seat0") == 0)
        bus_activate_session(s->session_object);

    /* Wait until logind marks the session active.  Without this the
     * compositor can race logind and get ENODEV from TakeDevice. */
    if (wait_session_active(s->session_id) < 0) {
        log_error("%s: session never became active; tearing down", s->name);
        close(sync_pipe[1]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        session_cleanup(s);  /* close fifo + clear session state */
        return -1;
    }

    /* Wait for udev to finish applying device ACLs before the compositor
     * tries to open DRM/input devices. */
    log_debug("%s: waiting for udevadm settle", s->name);
    wait_udev_settle();

    /* Signal the child to proceed by closing the write end. The child will
     * unblock from its read() and continue with privilege drop and exec. */
    close(sync_pipe[1]);

    s->state = is_greeter ? SEAT_GREETER : SEAT_SESSION;
    log_info("%s: %s started (pid=%d vt=%d uid=%u)",
             s->name, label, (int)pid, s->vtnr, (unsigned)pw->pw_uid);
    return 0;
}

int session_start(struct seat *s)
{
    return session_start_impl(s, 0);
}

int session_start_greeter(struct seat *s)
{
    return session_start_impl(s, 1);
}

void session_stop(struct seat *s)
{
    log_debug("session_stop for seat %s (child already reaped)", s->name);
    session_cleanup(s);
}

void session_shutdown(struct seat *s)
{
    if (s->compositor_pid <= 0) {
        session_cleanup(s);
        return;
    }

    log_info("%s: shutting down compositor pid=%d",
             s->name, s->compositor_pid);
    kill(s->compositor_pid, SIGTERM);

    /* Poll waitpid for up to 5 seconds (50 × 100 ms). */
    const int MAX_POLLS = 50;
    const int POLL_US   = 100000; /* 100 ms */

    for (int i = 0; i < MAX_POLLS; i++) {
        int wstatus;
        pid_t r = waitpid(s->compositor_pid, &wstatus, WNOHANG);
        if (r > 0) {
            log_info("%s: compositor exited after SIGTERM (waited %d ms)",
                     s->name, i * 100);
            session_cleanup(s);
            return;
        }
        if (r < 0) {
            /* ECHILD: already reaped by SIGCHLD handler */
            log_debug("%s: waitpid: %s", s->name, strerror(errno));
            session_cleanup(s);
            return;
        }
        usleep(POLL_US);
    }

    /* Compositor did not exit within 5 seconds — escalate. */
    log_warn("%s: compositor pid=%d did not exit after 5 s, sending SIGKILL",
             s->name, s->compositor_pid);
    kill(s->compositor_pid, SIGKILL);
    waitpid(s->compositor_pid, NULL, 0);
    session_cleanup(s);
}
