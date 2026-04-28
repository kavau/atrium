# atrium - Architecture

This document describes only those parts of the display manager that are currently implemented. It will be expanded as the implementation proceeds.

## High-Level Overview

The display manager's job is first of all to establish a user session, which requires the following steps:

1. (`seat0` only) VT allocation
2. PAM authentication (incl. `CreateSession`)
3. Compositor launch in its own process

## Components

### PAM Authentication (`daemon/auth/auth.c`)

Authentication is handled via PAM (Pluggable Authentication Modules). The authentication flow proceeds through the following stages:

1. `pam_start` - initializes the PAM context
2. `pam_authenticate` - authenticate the user
3. `pam_acct_mgmt` - verifies account validity
4. `pam_setcred` - manages additional credentials
5. `pam_open_session` - sets up a user session

The `pam_handle` acquired in the process must be maintained for the duration of the user session. On successful completion of the authentication flow, PAM delivers a list of environment variables that must be applied to the login session.

When the user session completes, the PAM session is wrapped up via `pam_close_session` followed by `pam_setcred(PAM_DELETE_CRED)` and `pam_end`.

### Session Creation (`daemon/core/session.c`)

Before starting a user session, the display manager must call the the logind `CreateSession` IPC via the D-Bus. Among other initialization tasks, this will grant the session access to the seat's input/output devices. We [do not need to execute this IPC directly](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html), but instead rely on PAM (in particular the `pam_systemd` module) to perform this step for us. This simplifies our code significantly, since no direct D-Bus communication is necessary. There are a few imporant points, however:

- The PAM authentication flow (to be precise, `pam_loginuid`) must not be executed in the daemon process, but in a child process (the session runner). It sets process-scoped session parameters (in particular `loginuid`) which must not leak into the daemon.

- `pam_systemd` uses the PID of the calling process as the session leader, which in our case will be the session helper. Hence the session runner must be kept alive for the duration of the session.

- Seat, session type, and session class need to be provided explicitly to the PAM environment via `pam_putenv()`.

## Compositor launch (`daemon/core/session.c`)

Session creation involves asynchronous processes, hence we must wait until the logind session is fully activated before starting the compositor. This is done over the D-Bus via `sd_session_is_active(session_id)`.

After the logind session is confirmed to be active, we fork a child process that will eventually become the user's login shell running the compositor. Within the child process, there are a few important steps that need to be taken care of first, however:

1. *Privilege drop*: set the child's `uid`, `gid`, and supplementary groups to the corresponding values of the user. This is the most critical step - omitting it would instantly grant the user root privileges.

2. *Home directory*: we need to explicitly set the working directory of the child process to the user's home directory.

3. *Login shell*: the compositor must be executed within the user's login shell, so user-specific configuration files (`~/.profile`) are loaded and PATH is configured correctly.

### Virtual Terminals (`daemon/core/vt.c`)

Virtual Terminals exist only on `seat0`, the concept does not exist for other seats. Before starting a session on `seat0` we must allocate a Virtual Terminal (VT) via the `VT_OPENQRY` ioctl. atrium allocates the VT when seat0 is first discovered and holds it for the lifetime of the seat. This guarantees that the VT number is stable.

The VT keyboard must be suppressed, so keystrokes from the graphical session don't leak into the TTY's input buffer.

### Session Runner (`daemon/core/session.c`)

As mentioned in *Session Creation*, the PAM authentication flow must not be executed in the daemon process. PAM sets process-specific environment variables that must not pollute the daemon's environment. Furthermore, the process calling `CreateSession` will become the session leader. This should be a session-specific process, not the daemon.

Hence, before creating a new session, we fork a *session runner* process from the daemon, which will become the session leader and manage the session lifecycle.

Once the session compositor exits, the helper tears down the login session and exits as well. This signals the daemon that the session has completed.
