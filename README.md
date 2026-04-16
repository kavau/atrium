# atrium

A Wayland display manager for Linux with first-class multiseat support.
Discovers seats via logind, launches a GTK4 greeter on each seat inside a
[cage](https://github.com/cage-kiosk/cage) kiosk compositor, and hands off
to a user-selected Wayland session.

> **Status: v0.1.0 — functional, pre-authentication.**
>
> atrium is still in early development, so expect rough edges and missing
> features. That said, atrium is usable as a daily-driver display manager
> today. The core workflow — greeter on every seat, user selection, session
> launch, and automatic greeter restart on logout — is fully operational. The
> main gap is authentication: PAM is not yet wired up, so any credentials are
> accepted.
>
> Known limitations:
> - **No authentication** — PAM integration is next (passwords are currently ignored)
> - **No hotplug** — seats added/removed after startup are not detected
> - **Compile-time config only** — all settings live in `src/config.h`
> - **No `CanGraphical` gating** — monitorless seats crash-loop the greeter

See [doc/architecture.md](doc/architecture.md) for a detailed design overview.
For multiseat hardware setup, see the
[Debian Multi-Seat HOWTO](https://wiki.debian.org/Multi_Seat_Debian_HOWTO).

---

## Dependencies

| Package | Purpose |
|---|---|
| `libsystemd` | logind session management (D-Bus via sd-bus) |
| `libudev` | seat and device hotplug events |
| `libpam` | user authentication |
| `gtk4` | greeter UI |
| `cage` | Wayland compositor hosting the greeter |
| `meson`, `ninja` | build system |

On Debian/Ubuntu:

```sh
apt install libsystemd-dev libudev-dev libpam0g-dev libgtk-4-dev cage meson ninja-build
```

On CachyOS/Arch (`libsystemd` and `libudev` are both provided by the `systemd` package):

```sh
pacman -S systemd pam gtk4 cage meson ninja
```

---

## Building and Installation

atrium runs as a systemd service (as root) and must be the only display manager
active on the system.

### 1. Configure

All settings are compile-time constants in `src/config.h`. Edit before building:

| Setting | Default | Purpose |
|---|---|---|
| `CONFIG_COMPOSITOR` | `"sway"` | Compositor/session to launch after login |
| `CONFIG_DESKTOP_NAME` | `"atrium-dev"` | Desktop identifier for logind `CreateSession` |
| `CONFIG_GREETER_UID/GID` | `1000` | User account that runs the greeter process |
| `CONFIG_SEAT_ENUM_DELAY` | `2` | Seconds to wait for logind seat discovery at boot |
| `CONFIG_RESTART_DELAY` | `5` | Seconds before restarting a crashed compositor |

`CONFIG_COMPOSITOR` can be set to the `Exec` value from any `.desktop` file in
`/usr/share/wayland-sessions/` (e.g. `sway`, `labwc`, `plasma-wayland`, `gnome-session`).
X11 sessions (`/usr/share/xsessions/`) are not supported.

> **Note:** `CONFIG_GREETER_UID/GID` should ideally be a dedicated system
> account (e.g. `atrium`). Using a regular user account works but is a
> shortcut.

### 2. Build and install

```sh
meson setup build
ninja -C build
sudo ninja -C build install
```

This installs:
- `/usr/local/bin/atrium` — the daemon
- `/usr/local/libexec/atrium-greeter` — the GTK4 greeter
- `/usr/lib/systemd/system/atrium.service` — the systemd unit

### 3. Enable and start

```sh
# Disable any existing display manager
sudo systemctl disable gdm   # or sddm, lightdm, etc.

# Enable atrium (the unit aliases to display-manager.service)
sudo systemctl enable atrium
```

Then reboot. atrium will start on boot and launch a greeter on every seat.

> **Warning:** Using `enable --now` or `disable --now` will immediately
> start/stop the display manager, killing any active graphical session.

---

## Development

### Remote deploy

The `tools/deploy.sh` script syncs the source tree to a remote machine, builds
there, and optionally installs and restarts the service (`/path/to/dest` can be
any temp directory):

```sh
# Build only
./tools/deploy.sh user@host:/path/to/dest

# Build, install, and restart
./tools/deploy.sh user@host:/path/to/dest -r
```

### Local testing with systemd-run

atrium must run outside any existing logind session cgroup, or `CreateSession`
will fail with "Already running in a session or user slice". Use `systemd-run`
to place it in a fresh transient unit:

```sh
# Stop any running display manager first
sudo systemctl stop atrium   # or gdm, sddm, etc.

# Launch atrium as a transient service (output goes to journal)
sudo systemd-run --unit=atrium-test /usr/local/bin/atrium

# Follow the logs
sudo journalctl -u atrium-test -f

# Stop
sudo systemctl stop atrium-test

# If re-running after a failure:
sudo systemctl reset-failed atrium-test
```

### What to verify

- The greeter (user picker) appears on every seat.
- Selecting a user launches the configured compositor.
- Logging out of the compositor restarts the greeter.
- `loginctl list-sessions` shows an active session for each seat.
- `sudo systemctl stop atrium` shuts down cleanly: compositors are
  killed and logind sessions are closed.

---

## Project Layout

```
src/       daemon source (event loop, seat/session/auth/bus/vt/greeter)
greeter/   atrium-greeter: GTK4 login UI
tools/     diagnostic and integration-test binaries
data/      installed config files (PAM stack, systemd unit)
doc/       architecture guide, implementation plan
build/     Meson build output (git-ignored)
```
