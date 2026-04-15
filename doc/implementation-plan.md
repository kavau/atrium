---
Created: April 12, 2026
Last Updated: April 15, 2026
---

# atrium — Implementation Plan

Each phase produces a working, buildable binary. Phases are intended to be
completed sequentially; later phases build directly on earlier ones. After each
phase the binary should be tested on a target machine before proceeding.

The plan is organized around an early milestone: **atrium enumerates all seats
at startup, creates a logind session for each one, and launches a Wayland
compositor — with no login UI or authentication.** Reaching this milestone
quickly validates the core architecture end-to-end (event loop, seat discovery,
logind integration, VT allocation, compositor management). Shortcuts taken to
get there are tagged `/* SHORTCUT */` in the code and listed explicitly in each
phase. Post-milestone phases remove them one by one.

---

## Pre-Milestone Phases

### Phase 1 — Event Loop Foundation

**Goal:** A daemon that starts, runs an epoll loop, and shuts down cleanly on
SIGTERM.

- `src/event.c` / `src/event.h` — epoll wrapper: register/unregister fds with
  callbacks, single `event_loop_run()` entry point.
- `src/main.c` — set up `signalfd` for `SIGTERM` and `SIGCHLD`; enter the event
  loop; log and exit on `SIGTERM`.

**Verification:** Run `atrium`; confirm it blocks. Send `SIGTERM`; confirm it
exits cleanly.

---

### Phase 2 — Seat Discovery

**Goal:** The daemon enumerates seats at startup and logs each one found.

- `src/seat.c` / `src/seat.h` — `struct seat`, seat list, initial udev
  enumeration.
- Wire seat list into the event loop.

**Shortcuts:**
- `/* SHORTCUT */` No hotplug monitoring; seats are enumerated once at startup
  only.

**Verification:** Run `atrium`; confirm `seat0` (and any additional seats) are
logged.

---

### Phase 3 — D-Bus / logind Interface

**Goal:** The daemon can communicate with logind: query seats and call
`CreateSession`.

- `src/bus.c` / `src/bus.h` — open the system bus via `sd-bus`; implement
  `GetSeat` and `CreateSession`.
- Wire the sd-bus fd into the event loop.

**Verification:** Run `atrium`; confirm it queries logind for each discovered
seat without errors.

---

### Phase 4 — VT Allocation (seat0)

**Goal:** The daemon can allocate and release a VT on seat0.

- `src/vt.c` / `src/vt.h` — `VT_OPENQRY` to find a free VT, open
  `/dev/ttyN`, release on teardown.
- Integrate into the seat0 code path; log the allocated VT number.

**Verification:** Run `atrium`; confirm a VT is allocated and visible in
`loginctl seat status seat0`.

---

### Phase 5 — Session Launch (Hardcoded User)

**Goal:** The daemon creates a logind session and launches a compositor on each
seat without a greeter or authentication.

- `src/session.c` / `src/session.h` — call `CreateSession` via logind; activate
  the session (VT switch on seat0); fork/exec the compositor.
- `SIGCHLD` handler — reap the compositor, wait 500 ms `/* SHORTCUT */`, then
  restart.

**Shortcuts:**
- `/* SHORTCUT */` Username is hardcoded per seat (no greeter, no PAM).
- `/* SHORTCUT */` On compositor exit, restart after a fixed 500 ms delay
  instead of proper crash-loop detection.

**Verification:** Run `atrium`; confirm the compositor launches on each seat and
the seat switches to it. Kill the compositor; confirm it restarts after ~500 ms.

---

## ★ Milestone — Headless Session Launch

> **atrium enumerates all seats at startup, creates a logind session for each
> one, and launches a Wayland compositor. When the compositor exits it is
> restarted. No login UI or authentication is involved.**

This milestone validates the full daemon lifecycle — event loop, seat discovery,
logind integration, VT allocation, and compositor management — without the
complexity of a greeter. Everything after this point replaces shortcuts with
production-quality implementations.

---

## Post-Milestone Phases

### Phase 6 — Session Environment

**Goal:** The compositor session is launched with a complete, correct
environment — matching what a normal login session would provide.

- **Login shell wrapping** — exec the compositor via the user's login shell
  (`execlp(pw->pw_shell, "-sh", "-l", "-c", "exec compositor", NULL)`).
  This sources `.profile` / `.bash_profile`, giving the compositor and its
  children the user's `PATH`, locale settings, and any other shell-level
  customization. Without this, everything inherits the daemon's root
  environment.
- **Desktop identity variables** — set `XDG_CURRENT_DESKTOP` and
  `XDG_SESSION_DESKTOP` so XDG desktop portals select the correct backend
  and desktop components can identify their session.
- **D-Bus session bus** — set `DBUS_SESSION_BUS_ADDRESS` to
  `unix:path=/run/user/<uid>/bus`. Most libraries auto-detect this on
  systemd, but some applications check the environment variable explicitly.

**Verification:** Run `atrium`; open a terminal inside the compositor and
verify `$PATH` matches a normal login session. Confirm `$XDG_CURRENT_DESKTOP`
and `$DBUS_SESSION_BUS_ADDRESS` are set.

---

### Phase 7 — Hotplug Monitoring

**Goal:** The daemon reacts to seats being added or removed at runtime.

- Extend `src/seat.c` — add the udev monitor fd to the event loop; handle
  `add` / `remove` uevent actions.
- **`CanGraphical` gate** — query logind's `CanGraphical` property on each
  seat before starting a session. Skip seats where the property is false
  (no monitor attached). Subscribe to `PropertiesChanged` to start/stop
  sessions when monitors are plugged or unplugged.

*Removes the `/* SHORTCUT */` from Phase 2.*

**Verification:** Run `atrium`. Plug/unplug a USB seat device; confirm the event
is detected and the seat is started/stopped accordingly. Confirm monitorless
seats are skipped cleanly.

---

### Phase 8 — Greeter Process Lifecycle

**Goal:** The daemon spawns cage (hosting a placeholder child) on each seat and
restarts it on exit.

- `src/greeter.c` / `src/greeter.h` — create credential/result pipes, fork cage
  with the correct environment (`WAYLAND_DISPLAY`, seat, VT), track the cage
  PID.
- Crash-loop detection — count rapid restarts per seat; back off and log an
  error if exceeded.

*Removes the 500 ms restart shortcut from Phase 5.*

**Verification:** Run `atrium`; confirm cage appears. Kill cage manually; confirm
it restarts. Kill it repeatedly; confirm crash-loop detection triggers.

---

### Phase 9 — Greeter UI (atrium-greeter)

**Goal:** A GTK4 login window runs inside cage on each seat.

- `greeter/main.c` — parse pipe fds from argv, GLib main loop.
- `greeter/ui-gtk4.c` — login window: username field, password field, submit
  button.
- `greeter/meson.build` — build `atrium-greeter` linking against gtk4.
- Replace the placeholder in Phase 8 with `atrium-greeter`.

**Verification:** Run `atrium`; confirm the login window appears on each seat.

---

### Phase 10 — Greeter IPC and PAM Authentication

**Goal:** Credentials from the greeter are authenticated via PAM before a
session is created.

- Greeter side: write `username\0password\0` to the credential pipe on submit;
  read a result byte; display error or proceed.
- `src/auth.c` / `src/auth.h` — drive a PAM conversation with the received
  credentials.
- Daemon side: read credentials from the pipe, call auth, write result back;
  only call `CreateSession` on success.

*Removes the hardcoded-username shortcut from Phase 5/6.*

**Verification:** Enter valid credentials; confirm the compositor launches.
Enter invalid credentials; confirm the greeter shows an error.

---

### Phase 11 — System Integration

**Goal:** atrium runs correctly as a production systemd service.

- `data/atrium.pam` — PAM stack configuration.
- `data/atrium.service` — systemd unit.
- `data/meson.build` — install PAM config and systemd unit.
- `deploy.sh` — build, install, restart unit.

**Verification:** `systemctl start atrium`; complete a full login cycle. Enable
the unit and reboot; confirm it takes over at boot.

---

### Phase 12 — Multiseat End-to-End

**Goal:** Two independent seats run simultaneously with no interference.

- Exercise the full login flow on each seat concurrently.
- Verify session isolation: logging out of one seat does not affect the other.
- Test hotplug: remove and reattach a seat's devices while the other seat has
  an active session.

**Verification:** Full manual end-to-end test with two seats active.

---

## Testing Philosophy

There is no automated test suite. Quality is maintained through:

- **Tool binaries** (`tools/`) — purpose-built diagnostic programs that exercise
  one subsystem in isolation (e.g. a VT allocator exerciser, a udev event
  logger). Add one for any non-trivial bug fix.
- **Live testing on the target machine** — the only meaningful integration test.
- **`assert()` guards** — permanent invariant checks at subsystem boundaries
  (e.g. asserting `vtnr == 0` for non-seat0 `CreateSession` calls). These are
  not debug aids; they document and enforce the contract.
