# atrium - Architecture

This document describes only those parts of the display manager architecture
that are currently implemented. It will be expanded as the implementation
proceeds.

## High-Level Overview

On startup, the display manager enumerates all available seats via logind and
allocates a virtual terminal (VT) for the default seat (seat0). It then forks a
*session runner* process on each seat, which manages the full session lifecycle
for that seat.

The session runner launches a greeter, waits for the greeter to respond with
user credentials, and negotiates user authentication with PAM. On successful
authentication, the greeter exits and the session runner launches a compositor
for the user session. When the user session ends, the session runner terminates,
and a new session runner is launched by the daemon.

```text
┌─────────────────────────────────────────────┐
│ Daemon                                      │
├─────────────────────────────────────────────┤
│  1. Enumerate seats                         │
│  2. VT allocation (seat0 only)              │
│  3. Launch a session runner on each seat    │
│    ┌───────────────────────────────────┐    │
│    │ Session runner                    │    │
│    ├───────────────────────────────────┤    │
│    │              ┌────────────┐       │    │
│    │  4. Launch   │ Greeter    │       │    │
│    │              └────────────┘       │    │
│    │  5. PAM authentication            │    │
│    │              ┌────────────┐       │    │
│    │  6. Launch   │ Compositor │       │    │
│    │              └────────────┘       │    │
│    └───────────────────────────────────┘    │
│  7. Event loop                              │
└─────────────────────────────────────────────┘
```

Boxes represent child processes. Each step is explained in detail below.

## Components

### Seat Enumeration

In order to find out which seats exist on the system, we call logind's
`ListSeats` method via the D-Bus.

Since at boot atrium's `ListSeats` query races with logind's own device
enumeration, seats may not yet be registered when atrium starts. The current
workaround is to sleep for a short time before enumerating seats; this will
eventually be replaced by seat hotplug detection, so atrium discovers new seats
as logind registers them.

### Virtual Terminals (`daemon/core/vt.c`)

Virtual Terminals exist only on `seat0`; the concept does not exist for other
seats. Before starting a session on `seat0` we must allocate a Virtual Terminal
(VT) via the `VT_OPENQRY` ioctl. atrium allocates the VT at startup (TODO: VT
allocation should instead take place when seat0 is first discovered) and holds
it for the lifetime of the seat. This guarantees that the VT number is stable.

The VT keyboard must be suppressed, so keystrokes from the graphical session
don't leak into the TTY's input buffer.

### Session Runner (`daemon/session/session_runner.c`)

As detailed below in *User Session Creation*, the PAM authentication flow must
not be executed in the daemon process. PAM sets process-specific environment
variables that must not pollute the daemon's environment. Furthermore, the
process calling `CreateSession` will become the session leader. This should be a
session-specific process, not the daemon.

Hence, before creating a new session, we fork a *session runner* process from
the daemon, which will become the session leader and manage the session
lifecycle (including the greeter) for a particular seat.

Once the session compositor exits, the session runner tears down the login
session and exits as well. This signals the daemon that the session has
completed.

### Greeter (`daemon/session/session_greeter.c`)

When the session runner starts, it first launches a greeter subprocess. The
greeter's role is to display a UI (inside a `cage` Wayland compositor session)
and collect the user's credentials (username and password). It sends the
credentials back to the session runner, which handles authentication and
notifies the greeter of either success (in which case the greeter exits) or
failure (in which case it prompts the user for credentials again).

Since the greeter needs access to the seat's DRM device, we must create a
systemd-logind session, so that logind can grant device access. Contrary to what
we do for a user session, we call logind CreateSession explicitly instead of
going through PAM (see *User Session Creation* below). `pam_systemd` would tie
the session to the calling process, and we would have to fork another dedicated
subprocess just for this purpose. Calling `bus_create_session()` directly avoids
this fork.

Communication with the greeter takes place over a pair of anonymous pipes, one
for each direction. The greeter sends `username\0password\0`, to which the
session runner responds with either `ok\n` or `fail:<reason>\n`.

### PAM Authentication (`daemon/session/auth.c`)

User authentication is handled via PAM (Pluggable Authentication Modules). The
authentication flow proceeds through the following stages:

1. `pam_start` - initializes the PAM context
2. `pam_authenticate` - authenticate the user
3. `pam_acct_mgmt` - verifies account validity
4. `pam_setcred` - manages additional credentials
5. `pam_open_session` - sets up a user session

The last step, `pam_open_session`, locks the current process into a new cgroup
and (if `pam_loginuid` is included in the PAM stack) writes to
`/proc/self/loginuid`, and must therefore run in a dedicated child process (the
other steps could in principle run in the daemon).

The `pam_handle` acquired in the process must be maintained for the duration of
the user session. On successful completion of the authentication flow, PAM
delivers a list of environment variables that must be applied to the login
session.

When the user session completes, the PAM session is wrapped up via
`pam_close_session` followed by `pam_setcred(PAM_DELETE_CRED)` and `pam_end`.

### User Session Creation (`daemon/session/session_runner.c`)

Before starting a user session, the display manager must call the logind
`CreateSession` IPC via the D-Bus. Among other initialization tasks, this will
grant the session access to the seat's input/output devices. We [do not need to
execute this IPC
directly](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html),
but instead rely on PAM (in particular the `pam_systemd` module) to perform this
step for us. This simplifies the code significantly, since no direct D-Bus
communication is necessary. There are a few important points, however:

- The PAM authentication flow (to be precise, `pam_open_session`) must not be
  executed in the daemon process, but in a child process (the session runner).
  It determines the process's cgroup and sets process-scoped session parameters
  (if `pam_loginuid` is included in the PAM stack) which must not leak into the
  daemon.

- `pam_systemd` uses the PID of the calling process as the session leader, which
  in our case will be the session runner. Hence the session runner must be kept
  alive for the duration of the session.

- Seat, session type, and session class need to be provided explicitly to the
  PAM environment via `pam_putenv()`.

### Compositor launch (`daemon/session/session_runner.c`)

Session creation involves asynchronous processes, hence we must wait until the
logind session is fully activated before starting the compositor. This is done
via `sd_session_is_active(session_id)` which reads the session state directly
from `/run/systemd/`.

After the logind session is confirmed to be active, we fork a child process that
will eventually become the user's login shell running the compositor. Within the
child process, there are a few important steps that need to be taken care of
first, however:

1. *Privilege drop*: set the child's `uid`, `gid`, and supplementary groups to
   the corresponding values of the user. This is the most critical step -
   omitting it would instantly grant the user root privileges.

2. *Home directory*: we need to explicitly set the working directory of the
   child process to the user's home directory.

3. *Login shell*: the compositor must be executed within the user's login shell,
   so user-specific configuration files (`~/.profile`) are loaded and PATH is
   configured correctly.

### Event loop

At startup, atrium blocks SIGTERM and SIGCHLD interrupts, so that these signals
are delivered synchronously via a `signalfd` file descriptor. The event loop
then polls this fd, becoming active when one of the signals arrives. This allows
us to avoid the main difficulties inherent in asynchronous signal handlers.

atrium's children are session-runner processes, which handle the greeter + user
session lifecycle for a seat. When the user session ends, the session runner
exits, causing atrium to be notified via SIGCHLD; atrium then starts a new
session runner for this seat, ensuring that no context (PAM, env, or fds) is
carried over from the previous session.
