# atrium - Architecture

This document describes the most important aspects of the display manager
architecture, as they are currently implemented. It will be updated as the
implementation proceeds. The focus is on the core architecture, the doc
therefore does not cover details such as the UI framework or config files.

## What Is A Display Manager

A display manager consists of two components:

* A **daemon** that starts at boot, and is responsible for user authentication
  and session lifecycle management.
* A graphical user interface (the **greeter**) for user selection and password
  entry.

Once a user successfully authenticates, the display manager launches a **user
session** running the user's chosen graphical desktop environment or window
manager.

## Design Goals

atrium aims to be a simple, correct display manager dedicated to multiseat
setups.

While most display managers treat multiseat as an afterthought, atrium is
designed from the ground up around the logind seat model.

Focusing on simplicity, atrium explicitly targets Linux systems based on
systemd/logind and Wayland compositors only. This scope limitation allows
atrium's code to remain clean and the multiseat logic to stay tractable.

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
┌───────────────────────────────────────────────────┐
│ Daemon                                            │
├───────────────────────────────────────────────────┤
│  1. Enumerate seats                               │
│  2. VT allocation (seat0 only)                    │
│  3. Event loop:                                   │
│      4. Launch a session runner on each seat      │
│      ┌─────────────────────────────────────────┐  │
│      │ Session runner                          │  │
│      ├─────────────────────────────────────────┤  │
│      │                         ┌────────────┐  │  │
│      │  5. Launch greeter      │ Greeter    │  │  │
│      │                         └────────────┘  │  │
│      │  6. PAM authentication                  │  │
│      │                         ┌────────────┐  │  │
│      │  7. Launch user session │ Compositor │  │  │
│      │                         └────────────┘  │  │
│      └─────────────────────────────────────────┘  │
└───────────────────────────────────────────────────┘
```

Nested boxes represent child processes. Each component is explained in detail
below.

## Components

### 1. Seat Enumeration (`daemon/core/bus.c`)

Seats are discovered by calling logind's `ListSeats` method via the D-Bus. A
session runner (see below) is started immediately on each detected seat that has
a display connected.

At boot atrium's `ListSeats` query races with logind's own device enumeration,
so seats may not yet be registered when atrium starts. The daemon subscribes to
D-Bus `SeatNew` events, so these seats will be detected as soon as logind
registers them (see the Event loop section below).

### 2. Virtual Terminals (`daemon/core/vt.c`)

Virtual Terminals exist only on `seat0`; the concept does not exist for other
seats. Before starting a session on `seat0` we must allocate a Virtual Terminal
(VT) via the `VT_OPENQRY` ioctl. atrium allocates the VT at startup and holds it
for the lifetime of the seat. This guarantees that the VT number is stable.

The VT keyboard must be suppressed, so keystrokes from the graphical session
don't leak into the TTY's input buffer.

### 3. Event loop (`daemon/core/main.c`)

At startup, atrium blocks SIGTERM and SIGCHLD interrupts, so that these signals
are delivered synchronously via a `signalfd` file descriptor. The event loop
then polls this and other fds, becoming active when one of the signals arrives.
This avoids the main difficulties inherent in asynchronous signal handlers.

atrium's children are session-runner processes, which handle the greeter + user
session lifecycle for a seat. When the user session ends, the session runner
exits, causing atrium to be notified via SIGCHLD; atrium then starts a new
session runner for this seat, ensuring that no context is carried over from the
previous session.

In addition to the signal fd above, the event loop polls various fds
related to hotplug detection and restart delays:

* A DRM monitor fd in order to detect newly connected displays: any seats that
  have no connected display are skipped initially and left idle. On a DRM change
  event, each idle seat is re-evaulated and a session runner is started if the
  seat has acquired a display.
* A D-Bus fd for `SeatNew` hotplug detection: if a seat was not detected at
  startup, or if it was established later (via `loginctl attach`), a session
  runner is started as soon as the seat becomes available.
* A per-seat timer fd: in order to prevent tight crash-loops, a session runner
  that exits abnormally is restarted only after a short delay. The timer fd is
  used to implement this delay in a non-blocking way. If too many crashes occur
  in a configurable time interval, the seat is permanently left idle.

### 4. Session Runner (`daemon/session/session_runner.c`)

Before creating a new session, the daemon forks a *session runner* process,
which becomes the session leader and manages the session lifecycle (including
the greeter) for a particular seat. The reasons the session creation and PAM
flow must run in a dedicated child process rather than in the daemon are
detailed below in *User Session Creation*.

Once the session compositor exits, the session runner tears down the login
session and exits as well. This signals the daemon that the session has
completed.

An additional benefit is complete isolation between consecutive sessions: since
each session runner is a freshly forked process, no state (environment, fds, PAM
handles, or memory) can leak into the next session.

### 5. Greeter (`daemon/session/greeter.c`)

When the session runner starts, it first launches an *unprivileged* greeter
subprocess, running as a dedicated system user. The greeter's role is to display
a UI and collect the user's credentials (username and password). It sends the
credentials back to the session runner, which handles authentication and
notifies the greeter of either success (in which case the greeter exits) or
failure (in which case it prompts the user for credentials again).

The greeter needs access to the seat's DRM device. Therefore a systemd-logind
session needs to be created so that logind can grant device access. Unlike the
user session, the greeter session is created by calling `CreateSession` directly
instead of going through PAM (see *User Session Creation* below). `pam_systemd`
would tie the session to the calling process, and we would have to fork another
dedicated subprocess just for this purpose. Calling `bus_create_session()`
directly avoids this fork.

Communication with the greeter takes place over a pair of anonymous pipes, one
for each direction. The pipe fds are passed via two environment variables
`CREDENTIALS_FD` and `RESULT_FD`. The greeter sends
`username\0password\0session_id\0`, to which the session runner responds with
either `ok\n` or `fail:<reason>\n`.

atrium uses the `cage` Wayland compositor to run the greeter UI.

### 6. PAM Authentication (`daemon/session/auth.c`)

User authentication is handled via PAM (Pluggable Authentication Modules). The
authentication flow proceeds through the following stages:

1. `pam_start` - initializes the PAM context
2. `pam_authenticate` - authenticates the user
3. `pam_acct_mgmt` - verifies account validity
4. `pam_setcred` - manages additional credentials
5. `pam_open_session` - sets up a user session

The last step, `pam_open_session`, is the heavyweight here. It locks the current
process into a new cgroup and must therefore run in a dedicated child process.

The `pam_handle` acquired in the process must be maintained for the duration of
the user session. On successful completion of the authentication flow, PAM
delivers a list of environment variables that must be applied to the login
session.

When the user session completes, the PAM session is wrapped up via
`pam_close_session` followed by `pam_setcred(PAM_DELETE_CRED)` and `pam_end`.

### 7. User Session

#### Session Creation (`daemon/session/session_runner.c`)

Before starting a user session, the display manager must call the logind
`CreateSession` IPC via D-Bus. Among other initialization tasks, this will grant
the session access to the seat's input/output devices. atrium [does not execute
this IPC
directly](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html),
but instead relies on PAM (in particular the `pam_systemd` module) to perform
this step. This simplifies the code significantly, since no direct D-Bus
communication is necessary. Some important points are:

* The PAM authentication flow (to be precise, `pam_open_session`) must not be
  executed in the daemon process, but in a child process (the session runner).
  It determines the process's cgroup and sets process-scoped session parameters
  (if `pam_loginuid` is included in the PAM stack), which must not leak into the
  daemon.

* `pam_systemd` (via `CreateSession`) makes the calling process the session
  leader, and logind ties the session's lifetime to it. In our case this is the
  session runner, which must therefore stay alive for the duration of the
  session. This is a second reason why the PAM flow cannot run in the daemon:
  the session leader must be a dedicated, session-scoped process.

* Seat (`XDG_SEAT`), session type (`XDG_SESSION_TYPE`), and session class
  (`XDG_SESSION_CLASS`) are provided explicitly to the PAM environment via
  `pam_putenv()` before `pam_open_session` runs. Here `XDG_SEAT` matters most in
  a multiseat environment, as it connects the session to a seat, and logind
  only grants access to the devices associated with that seat.

#### Compositor launch (`daemon/session/compositor.c`)

Session creation involves asynchronous processes - the compositor must not be
started until the logind session is fully activated. This is done via
`sd_session_is_active(session_id)` which reads the session state directly from
`/run/systemd/`.

After the logind session is confirmed to be active, the session runner forks a
child process to run the compositor. Before exec'ing, the child performs the
following steps:

1. *Privilege drop*: set the child's `uid`, `gid`, and supplementary groups to
   the corresponding values of the user. This is the most critical step -
   omitting it would instantly grant the user root privileges.

2. *Environment*: set important environment variables such as `HOME`, `SHELL`,
   and `PATH`, among others.

3. *Home directory*: the child's working directory is set to the user's home
   directory.

After these steps, the child `exec`s the compositor in a non-interactive shell,
and the user is presented with a graphical session.
