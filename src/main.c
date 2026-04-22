#include "auth.h"
#include "bus.h"
#include "config.h"
#include "event.h"
#include "greeter.h"
#include "log.h"
#include "seat.h"
#include "session.h"
#include "vt.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

/* Returns 1 if username is valid for login, 0 otherwise (logs the reason).
 * Rejects empty, overlong, or non-portable-filename characters to prevent
 * ANSI escape injection in log output. */
static int is_valid_username(const char *username, size_t max_len,
                             const char *ctx)
{
    size_t ulen = strlen(username);
    if (ulen == 0 || ulen >= max_len) {
        log_error("%s: username %s", ctx, ulen == 0 ? "empty" : "too long");
        return 0;
    }
    for (size_t i = 0; i < ulen; i++) {
        unsigned char c = (unsigned char)username[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
            log_error("%s: invalid character in username", ctx);
            return 0;
        }
    }
    return 1;
}

/*
 * on_greeter_credentials — fires when the greeter writes credentials.
 *
 * Wire format: "<username>\0<password>\0" — two consecutive null-terminated
 * strings.  We authenticate the user via PAM, and on success store the
 * auth_result and reply "ok\n".  The greeter then exits cleanly, triggering
 * SIGCHLD which launches the compositor.
 *
 * SHORTCUT: reads the entire message in one read().  Safe for pipe writes
 * under PIPE_BUF (4096 bytes).
 */
static void on_greeter_credentials(int fd, void *userdata)
{
    struct seat *s = userdata;

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    ssize_t n = read(fd, buf, sizeof(buf) - 1);

    if (n == 0) {
        /* Greeter closed the pipe without submitting (exited or crashed). */
        log_info("credentials pipe closed on %s", s->name);
        return;
    }
    if (n < 0) {
        log_error("credentials pipe read on %s: %s", s->name, strerror(errno));
        return;
    }

    /* Locate the boundary: first '\0' separates username from password. */
    const char *username = buf;
    const char *password = NULL;
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\0' && i + 1 < n) {
            password = buf + i + 1;
            break;
        }
    }

    if (!password) {
        log_error("credentials(%s): malformed message", s->name);
        memset(buf, 0, sizeof(buf));
        greeter_send_result(s, "fail:Internal error\n");
        return;
    }

    if (!is_valid_username(username, sizeof(s->greeter_username), s->name)) {
        memset(buf, 0, sizeof(buf));
        greeter_send_result(s, "fail:Invalid username\n");
        return;
    }

    size_t ulen = strlen(username);
    log_info("login request on %s: user='%s'", s->name, username);

    /* Store username before wiping buf (username points into buf). */
    memcpy(s->greeter_username, username, ulen + 1);

    /* SHORTCUT: passwordless users bypass PAM entirely.  Resolve uid/gid
     * via getpwnam() and skip authentication.  No PAM session is opened:
     * s->auth.pamh stays NULL, so auth_close() and auth_open_session()
     * will be no-ops for this session. */
    static const char *passwordless[] = CONFIG_PASSWORDLESS_USERS;
    int is_passwordless = 0;
    for (size_t i = 0; i < sizeof(passwordless)/sizeof(passwordless[0]); i++) {
        if (passwordless[i] && strcmp(s->greeter_username, passwordless[i]) == 0) {
            is_passwordless = 1;
            break;
        }
    }

    if (is_passwordless) {
        /* Wipe buffer (contains empty password, but be consistent). */
        memset(buf, 0, sizeof(buf));

        errno = 0;
        struct passwd *pw = getpwnam(s->greeter_username);
        if (!pw) {
            log_error("getpwnam(%s): %s", s->greeter_username,
                      errno ? strerror(errno) : "user not found");
            s->greeter_username[0] = '\0';
            greeter_send_result(s, "fail:Unknown user\n");
            return;
        }
        s->auth = (auth_result){0};
        s->auth.uid = pw->pw_uid;
        s->auth.gid = pw->pw_gid;
        /* pamh stays NULL — auth_close/auth_open_session will skip PAM. */
        log_info("%s: passwordless login for '%s' (uid=%u gid=%u)",
                 s->name, s->greeter_username,
                 (unsigned)pw->pw_uid, (unsigned)pw->pw_gid);
        greeter_send_result(s, "ok\n");
        return;
    }

    /* Authenticate via PAM. */
    auth_result auth = {0};
    int pam_rc = auth_begin(username, password, &auth);

    /* Wipe credentials from the buffer immediately. */
    memset(buf, 0, sizeof(buf));

    if (pam_rc != PAM_SUCCESS) {
        log_error("%s: authentication failed for '%s'",
                  s->name, s->greeter_username);
        s->greeter_username[0] = '\0';
        greeter_send_result(s, "fail:Authentication failed\n");
        return;
    }

    s->auth = auth;

    greeter_send_result(s, "ok\n");
    /* Greeter will now exit(0), triggering SIGCHLD → session_start(). */
}

/* Event loop callback for the signalfd. Reads one signal and dispatches on
 * signal number. */
static void on_signal(int fd, void *userdata)
{
    (void)userdata;

    struct signalfd_siginfo si;
    ssize_t n = read(fd, &si, sizeof(si));
    if (n != sizeof(si)) {
        log_error("signalfd read: %s", strerror(errno));
        return;
    }

    switch (si.ssi_signo) {
    case SIGTERM:
        log_info("received SIGTERM, shutting down");
        event_loop_quit();
        break;
    case SIGCHLD: {
        /*
         * Reap all exited children.  For each, determine whether it was the
         * greeter or the compositor, and take the appropriate action:
         *   - Greeter exited cleanly (exit 0): start the compositor session.
         *   - Greeter crashed / compositor exited: restart the greeter.
         *
         * SHORTCUT: sleep() blocks the event loop on restart.  Phase 12
         * replaces this with timerfd-based crash-loop detection.
         */
        int wstatus;
        pid_t pid;
        log_debug("SIGCHLD received");
        while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
            if (WIFEXITED(wstatus))
                log_info("child %d exited with status %d",
                         (int)pid, WEXITSTATUS(wstatus));
            else if (WIFSIGNALED(wstatus))
                log_info("child %d killed by signal %d",
                         (int)pid, WTERMSIG(wstatus));

            /* Find the seat this pid belongs to. */
            struct seat *s = NULL;
            for (int i = 0; i < seat_count(); i++) {
                if (seat_get(i)->compositor_pid == pid) {
                    s = seat_get(i);
                    break;
                }
            }

            if (!s) {
                log_debug("reaped unknown pid %d", (int)pid);
                continue;
            }

            int was_greeter = (s->state == SEAT_GREETER);
            int exit_ok     = WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;

            if (was_greeter) {
                event_remove(s->credentials_rfd);
                greeter_stop(s);
            } else if (s->auth.pamh) {
                /* Compositor exited — close PAM session. */
                auth_close(&s->auth);
            }
            session_stop(s);

            if (was_greeter && exit_ok) {
                /* Greeter exited cleanly — start compositor session. */
                log_info("%s: greeter completed — starting compositor",
                         s->name);
                if (session_start(s) < 0)
                    log_error("%s: session_start failed", s->name);
            } else {
                /* Greeter crashed or compositor exited — restart greeter. */
                log_info("%s: %s exited — restarting greeter in %d s",
                         s->name,
                         was_greeter ? "greeter" : "compositor",
                         CONFIG_RESTART_DELAY);
                sleep(CONFIG_RESTART_DELAY); /* SHORTCUT */
                if (greeter_start(s) < 0)
                    log_error("%s: greeter_start failed", s->name);
                else if (event_add(s->credentials_rfd,
                                   on_greeter_credentials, s) < 0)
                    log_error("%s: event_add(credentials) failed", s->name);
            }
        }
        break;
    }
    default:
        assert(0 && "unexpected signal");
    }
}

int main(void)
{
    log_info("starting");
    log_debug("debug logging is enabled");

    /* Ignore SIGPIPE so a broken result pipe (greeter crashed between sending
     * credentials and reading the reply) returns EPIPE from write() instead of
     * terminating the daemon and killing every session on every seat. */
    signal(SIGPIPE, SIG_IGN);

    /* Block SIGTERM and SIGCHLD so they are delivered via signalfd only. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        log_error("sigprocmask: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Create a signalfd to receive the blocked signals as fd events. */
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0) {
        log_error("signalfd: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Register the signalfd with the event loop. */
    if (event_add(sfd, on_signal, NULL) < 0)
        return EXIT_FAILURE;
    log_debug("registered signalfd %d", sfd);

    /* Open the system bus and register it with the event loop. */
    if (bus_open() < 0)
        return EXIT_FAILURE;
    log_debug("bus connection established");

    /* Enumerate seats via logind.
     * SHORTCUT: sleep briefly to let logind finish processing udev seat
     * events on early boot.  Replaced by SeatNew/SeatRemoved signal
     * monitoring in Phase 6 (hotplug). */
    sleep(CONFIG_SEAT_ENUM_DELAY);
    log_debug("discovering seats...");
    if (bus_enumerate_seats() < 0)
        return EXIT_FAILURE;
    for (int i = 0; i < seat_count(); i++)
        log_info("found seat: %s", seat_get(i)->name);

    /* Allocate a VT for seat0. */
    log_debug("allocating VT for seat0...");
    for (int i = 0; i < seat_count(); i++) {
        struct seat *s = seat_get(i);
        if (strcmp(s->name, "seat0") != 0)
            continue;
        if (vt_alloc(&s->vtnr) < 0)
            return EXIT_FAILURE;
        log_info("seat0: allocated vt%d", s->vtnr);

        /* Suppress VT keyboard input so keystrokes typed into the Wayland
         * greeter/compositor don't leak into the TTY's input buffer. */
        s->vt_kb_fd = vt_suppress_keyboard(s->vtnr);
        if (s->vt_kb_fd < 0)
            log_warn("seat0: failed to suppress VT keyboard (continuing)");
        break;
    }

    /* Start the greeter on every seat.  The greeter collects credentials;
     * on success SIGCHLD triggers session_start for the compositor. */
    for (int i = 0; i < seat_count(); i++) {
        struct seat *s = seat_get(i);
        if (greeter_start(s) < 0) {
            log_warn("%s: greeter_start failed, skipping", s->name);
            continue;
        }
        if (event_add(s->credentials_rfd, on_greeter_credentials, s) < 0)
            log_warn("%s: event_add(credentials) failed", s->name);
    }

    /* Run until event_loop_quit() is called. */
    log_debug("entering main event loop");
    event_loop_run();

    /* Clean up. On early-exit error paths above, the process terminates and
     * the kernel releases all resources, so explicit cleanup is skipped. */
    log_debug("beginning cleanup sequence");
    /* Shut down all sessions and greeters. */
    for (int i = 0; i < seat_count(); i++) {
        struct seat *s = seat_get(i);
        if (s->state == SEAT_GREETER) {
            event_remove(s->credentials_rfd);
            greeter_stop(s);
        }
        session_shutdown(s);
        if (s->auth.pamh)
            auth_close(&s->auth);
    }

    /* Release VT allocations. */
    int vt_count = 0;
    for (int i = 0; i < seat_count(); i++) {
        struct seat *s = seat_get(i);
        if (s->vtnr > 0) {
            vt_restore_keyboard(s->vt_kb_fd);
            s->vt_kb_fd = -1;
            vt_release(s->vtnr);
            vt_count++;
        }
    }
    if (vt_count > 0)
        log_debug("released %d VT allocations", vt_count);

    bus_close();
    event_remove(sfd);
    close(sfd);

    log_info("stopped");
    return EXIT_SUCCESS;
}
