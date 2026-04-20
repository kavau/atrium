---
Created: April 12, 2026
Last Updated: April 20, 2026
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

### Phase 7 — Standalone Greeter App

**Goal:** A standalone GTK4 login window that communicates with a parent process
via pipes. Can be built and tested independently of the daemon.

- `greeter/main.c` — parse pipe fds from argv, GLib main loop.
- `greeter/ui-gtk4.c` — login window: username field, password field, submit
  button. Write `username\0password\0` to the credential pipe on submit; read
  a result byte; display error or proceed.
- `greeter/meson.build` — build `atrium-greeter` linking against gtk4.
- `tools/greeter-test` — a small launcher that creates pipes, forks the greeter,
  reads credentials, and writes a result byte. Validates the IPC protocol
  without involving the daemon.

**Verification:** Run `tools/greeter-test`; confirm the greeter appears, submit
credentials, confirm the launcher receives them and the greeter shows
success/failure based on the result byte.

---

### Phase 8 — Greeter Integration

**Goal:** The daemon shows the greeter on each seat, accepts a login, launches
the compositor, and returns to the greeter when the compositor exits. This
phase implements the full greeter↔compositor lifecycle.

- `src/greeter.c` / `src/greeter.h` — fork cage hosting `atrium-greeter` on
  each seat, create credential/result pipes, track the cage PID.
- Seat state machine: greeter running → user logs in → stop greeter, start
  compositor → compositor exits → restart greeter.
- Daemon reads credentials from the pipe, launches the compositor for that user.

**Shortcuts:**
- `/* SHORTCUT */` No PAM authentication — password is ignored, login always
  succeeds (passwordless login).
- `/* SHORTCUT */` No crash-loop detection; greeter restarts unconditionally
  after a fixed delay.

*Removes the hardcoded-username shortcut from Phase 5/6.*

**Verification:** Run `atrium`; confirm the greeter appears on each seat. Enter
a username; confirm the compositor launches. Exit the compositor; confirm the
greeter reappears. Verify both seats work independently.

---

### Phase 9 — Standalone PAM Module

**Goal:** A PAM authentication module that can be tested independently of the
daemon.

- `src/auth.c` / `src/auth.h` — drive a PAM conversation: open a PAM handle,
  call `pam_authenticate`, call `pam_acct_mgmt` (account checks: expired,
  locked, etc.), close the handle.
- `data/atrium.pam` — PAM stack configuration.
- `tools/pam-test` — a small binary that takes username and password on stdin
  and calls the auth module. Validates PAM integration without involving the
  daemon.

**Verification:** Run `tools/pam-test` with valid credentials; confirm success.
Run with invalid credentials; confirm failure. Run with a locked/expired
account; confirm the appropriate error.

---

### Phase 10 — PAM Integration and End-to-End Validation

**Goal:** Wire PAM authentication into the greeter flow. Full login cycle on
multiple seats.

- Daemon reads credentials from the greeter pipe, calls `auth.c` to
  authenticate, writes result back. Only calls `CreateSession` on success.
- End-to-end validation on both seats: valid login, invalid login (error shown),
  logout (greeter reappears), concurrent sessions.

*Removes the passwordless-login shortcut from Phase 8.*

**Verification:** Enter valid credentials on each seat; confirm the compositor
launches. Enter invalid credentials; confirm the greeter shows an error.
Log out of one seat; confirm the other is unaffected.

---

### Phase 11 — Hotplug Monitoring

**Goal:** The daemon reacts to seats being added or removed at runtime, and
skips seats that have no display attached.

Two independent event sources are needed:

- **D-Bus signals** from logind: `SeatNew` / `SeatRemoved` for seat lifecycle;
  `PropertiesChanged` → `CanGraphical` for display readiness per seat.
- **udev monitor** on the `drm` subsystem: `change` events for connector
  plug/unplug (cable inserted/removed); `add`/`remove` for GPU hotplug.

`CanGraphical` alone is insufficient: testing confirmed it reports `true` for
a seat whose GPU has no monitor cabled — it only reflects whether a graphics
device is assigned to the seat, not whether a display is connected. The DRM
connector status files (`/sys/class/drm/cardN-*/status`) give the correct
signal (`connected` vs `disconnected`).

Implemented in five steps to verify each monitoring mechanism in isolation
before acting on it:

**Step 1** *(complete)*: D-Bus signal subscriptions — log only.
- `SeatNew` / `SeatRemoved` match rules; per-seat `PropertiesChanged` match
  on `CanGraphical`; `bus_query_can_graphical()` at enumeration.
- Verified on tux: `CanGraphical` queried and logged for each seat;
  subscriptions registered without error.

**Step 2**: udev DRM monitor — log only.
- Open a `udev_monitor` on the `drm` subsystem; register its fd with the
  event loop.
- Log all events: action (`change`/`add`/`remove`), devpath, `ID_SEAT`.
- Verify on tux by plugging/unplugging a cable and reading the journal.

**Step 3**: Act on D-Bus signals.
- `SeatNew`: `seat_add` + **call `bus_subscribe_properties_changed`** (currently
  omitted — dynamically-added seats get no `CanGraphical` tracking without
  this) + start greeter if `drm_seat_has_display()` true.
- `SeatRemoved`: stop greeter if `SEAT_GREETER`; log and leave if
  `SEAT_SESSION`.
- `PropertiesChanged` → `CanGraphical` true: start greeter if `SEAT_IDLE`
  and `drm_seat_has_display()` true.
- `PropertiesChanged` → `CanGraphical` false: stop greeter if
  `SEAT_GREETER`; leave session alone if `SEAT_SESSION`.
- Add `drm_seat_has_display()` static gate at initial greeter launch
  (enumeration path): defer seats with no connected display instead of
  crash-looping.

**Step 4**: Act on udev DRM events.
- Connector `change` (plug) → re-check `drm_seat_has_display()`; if now
  true and seat is `SEAT_IDLE`, start greeter.
- Connector `change` (unplug) → if `SEAT_GREETER`, stop greeter and return
  to `SEAT_IDLE`; if `SEAT_SESSION`, leave alone.
- GPU `remove` → treat as unplug (stop greeter; leave session).

**Step 5**: Remove shortcuts.
- Remove `CONFIG_SEAT_ENUM_DELAY` (startup sleep no longer needed — deferred
  seats wait for D-Bus/udev events instead).
- Remove `CONFIG_IGNORE_SEATS` (superseded by the `drm_seat_has_display()`
  gate).

**Verification:** Run `atrium` with all seats enabled. Confirm monitorless
seats are deferred at startup (no crash-loop). Plug in a cable; confirm the
greeter starts. Unplug; confirm the greeter stops. Hot-add a seat; confirm
greeter starts. Remove it; confirm clean teardown. Confirm `CONFIG_IGNORE_SEATS`
and `CONFIG_SEAT_ENUM_DELAY` are gone.

---

### Phase 12 — Crash-Loop Detection

**Goal:** Detect and back off from rapid compositor or greeter restarts.

- Count rapid restarts per seat; back off and log an error if a threshold is
  exceeded.
- Replace the fixed restart delay with `timerfd`-based tracking.

*Removes the fixed-delay restart shortcut from Phase 5/8.*

**Verification:** Kill the compositor or greeter repeatedly; confirm crash-loop
detection triggers and the daemon backs off instead of restarting indefinitely.

---

## Future Features

Features planned for implementation after the core phases are complete. No
particular order or timeline.

### Core / Daemon Improvements

- **Passwordless login and autologin** — allow per-seat configuration for
  passwordless accounts and automatic login (skip the greeter entirely for
  designated seats/users).
- **Session .desktop file support** — read `/usr/share/wayland-sessions/*.desktop`
  (and `/usr/share/xsessions/*.desktop`) to discover available sessions. Let the
  user pick a session type in the greeter instead of hardcoding the compositor.
- **X11 session support** — launch X11-based desktop environments from the
  greeter. The greeter itself remains Wayland (inside cage), but the user
  session would run under Xwayland or a standalone X server. Only if feasible
  without disproportionate effort.
- **Third-party greeter support** — allow using existing greeters from other
  display managers instead of atrium-greeter, primarily
  [greetd](https://git.sr.ht/~kennylevinsen/greetd) 
  (gtkgreet, tuigreet, ReGreet). Only if feasible without disproportionate
  effort.
- **Last user/session memory** — remember the last logged-in user and session
  type per seat. Pre-select them on the next greeter appearance. Persist to
  disk.

### Greeter Improvements

- **Shutdown/reboot/suspend from greeter** — allow the user to shut down,
  reboot, or suspend the machine from the greeter UI without logging in.
  Requires a greeter→daemon IPC command and logind `PowerOff`/`Reboot`/
  `Suspend` D-Bus calls.
- **User list in greeter** — show available users with names/avatars instead of
  a free-text username field. Read from AccountsService or enumerate non-system
  users from `/etc/passwd`.
- **Custom greeter themes** — allow user-configurable greeter appearance
  (background, colors, layout).
- **Basic accessibility** — on-screen keyboard and high-contrast mode in the
  greeter.
- **Greeter screen blanking** — turn off the display after a configurable idle
  timeout while the greeter is waiting for input. Wake on any input event.
  Reduces power consumption and avoids burn-in on always-on login screens.
  *(Implemented as SHORTCUT in v0.2.0 — black overlay, backlight stays on.)*
- **Show/hide password toggle** — button to reveal the password field contents
  while typing, to reduce frustration with long or complex passwords.
- **Keyboard layout switcher** — allow switching between configured system
  keyboard layouts from the greeter, for multi-layout setups. Read available
  layouts from the system configuration (e.g. `/etc/X11/xorg.conf.d/` or
  `localectl`) and send the selection to the compositor.
- **Clock display** — show the current date and time in the greeter UI,
  updated every minute.

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
