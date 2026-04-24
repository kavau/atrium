# atrium

A Wayland display manager for Linux with first-class multiseat support.
Discovers seats via logind, launches a GTK4 greeter on each seat inside a
[cage](https://github.com/cage-kiosk/cage) kiosk compositor, and hands off
to a user-selected Wayland session.

### Why atrium?

The Linux kernel and the low-level system stack — udev device tagging, logind
seat management, VT allocation — are fully capable of multiseat operation.
Wayland compositors are seat-agnostic by design: they simply claim whatever
display and input devices logind assigns to their seat. The missing piece has
always been the display manager. Historically, display managers have treated
multiseat as an afterthought. The implementations are brittle, poorly tested
on real hardware, and often impossible to get working reliably.

atrium aims to fill this gap. It is a display manager that puts multiseat
first: discover every seat, run a greeter on each one, authenticate the user,
and start the session — with correct seat lifecycle management throughout. The
project targets a modern, minimal setup: systemd/logind for session management,
PAM for authentication, and Wayland as the primary target. No historical
baggage.

> **Status: v0.2.0 — fully functional with authentication.**
>
> atrium is still in early development, so expect rough edges and missing
> features. That said, atrium is usable as a daily-driver display manager
> today. The core workflow — greeter on every seat, PAM authentication,
> logind session creation, compositor launch, and automatic greeter restart
> on logout — is fully operational.
>
> **Warning:** atrium runs as a system service with root privileges. It is
> experimental software — use at your own risk. See [LICENSE](LICENSE) for
> warranty and liability terms.
>
> See [Known Limitations](#known-limitations) below.

See [doc/architecture.md](doc/architecture.md) for a detailed design overview.

For instructions how to configure your hardware for multiseat,
see [doc/multiseat-setup.md](doc/multiseat-setup.md).

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

On Fedora (`systemd-devel` covers both `libsystemd` and `libudev`):

```sh
dnf install systemd-devel pam-devel gtk4-devel cage meson ninja-build
```

---

## Supported Distros

| Distro | Status |
|---|---|
| CachyOS / Arch Linux | Verified — primary development target |
| Debian / Ubuntu | PAM stack provided and tested |
| Fedora | PAM stack provided; untested |

Other systemd-based distros should work — the only distro-specific piece is
the PAM stack. Use whichever of the three provided configs most closely matches
your distro's PAM layout, or adapt one as needed.

---

## Building and Installation

atrium runs as a systemd service (as root) and must be the only display manager
active on the system.

> **Install a tagged release, not `main`:** `main` is the active development
> branch and may be broken at any time. For production use, download the
> latest release from
> [github.com/kavau/atrium/releases/latest](https://github.com/kavau/atrium/releases/latest).

### 1. Configure

All settings are compile-time constants in `src/config.h`. Edit before building:

| Setting | Default | Purpose |
|---|---|---|
| `CONFIG_COMPOSITOR` | `"sway"` | Compositor/session to launch after login |
| `CONFIG_DESKTOP_NAME` | `"sway"` | Desktop identifier for logind `CreateSession` |
| `CONFIG_GREETER_USER` | `"atriumdm"` | System account that runs the greeter process |
| `CONFIG_SEAT_ENUM_DELAY` | `2` | Seconds to wait for logind seat discovery at boot |
| `CONFIG_RESTART_DELAY` | `2` | Seconds before restarting a crashed compositor |

The following two values should come from the target session's `.desktop` file in
`/usr/share/wayland-sessions/`:

- `CONFIG_COMPOSITOR` — the `Exec=` field (e.g. `cosmic-session`, `sway`, `gnome-session`)
- `CONFIG_DESKTOP_NAME` — the `DesktopNames=` field (e.g. `COSMIC`, `sway`, `GNOME`)

`CONFIG_DESKTOP_NAME` is case-sensitive: it becomes `XDG_CURRENT_DESKTOP` in the
session environment, and desktop portals and shell components use it for exact-match
lookups. Using the wrong case (e.g. `"cosmic"` instead of `"COSMIC"`) will break
portal integration and other desktop-specific features.

X11 sessions (`/usr/share/xsessions/`) are not supported.

### 2. Build and install

```sh
meson setup build -Dpam_config=arch   # or debian, fedora
ninja -C build
sudo ninja -C build install
```

The `-Dpam_config` option selects which PAM stack to install (default: `arch`).
Use `debian` on Debian/Ubuntu systems, `fedora` on Fedora.

This installs:
- `/usr/local/bin/atrium` — the daemon
- `/usr/local/libexec/atrium-greeter` — the GTK4 greeter
- `/usr/lib/systemd/system/atrium.service` — the systemd unit
- `/etc/pam.d/atrium` — the PAM configuration

The install process also creates the `atriumdm` system user (if it doesn't already
exist), which runs the greeter process as an unprivileged account.

### 3. Enable and start

First, note which display manager is currently active so you can restore it if needed:

```sh
readlink /etc/systemd/system/display-manager.service
# e.g. /usr/lib/systemd/system/gdm.service  →  re-enable with: systemctl enable gdm
```

Then disable it and enable atrium:

```sh
sudo systemctl disable gdm   # substitute your current display manager
sudo systemctl enable atrium
```

Then reboot. atrium will start on boot and launch a greeter on every seat.

> **Warning:** Using `enable --now` or `disable --now` will immediately
> start/stop the display manager, killing any active graphical session.

> **Multiseat:** For a multiseat setup, configure your seats with `loginctl`
> before starting atrium. See [doc/multiseat-setup.md](doc/multiseat-setup.md)
> for a step-by-step guide.

### Uninstall

To fully remove atrium, switch to another display manager first (so the
next boot still has a graphical login) and then run the provided script.
Do this from a TTY (`Ctrl+Alt+F2`) to avoid killing your current desktop
session when the atrium service stops.

```sh
sudo systemctl disable atrium    # free the display-manager alias
sudo systemctl enable gdm        # substitute your previous display manager
sudo ./tools/uninstall.sh        # stop the service, remove files, delete the atriumdm user
```

The script expects the Meson build directory to be `build/`; pass a different
path as the second argument if yours lives elsewhere.

If the script reports that it could not remove the `atriumdm` user (lingering
processes can briefly hold it open after the service stops), simply run it
again after a short time — the second run will clean up the user once those
processes have exited.

---

## If Things Go Wrong

If atrium fails to start or you can't log in, switch back to your previous
display manager from a TTY (`Ctrl+Alt+F2`, log in as root or with `sudo`):

```sh
sudo systemctl disable atrium
sudo systemctl enable gdm   # substitute your previous display manager
sudo reboot
```

To reset all seat assignments and return to a single-seat configuration:

```sh
sudo loginctl flush-devices
```

To check what went wrong, inspect the journal:

```sh
sudo journalctl -u atrium -b   # logs from the current boot
sudo journalctl -u atrium -b-1 # logs from the previous boot
```

---

## Known Limitations

### Hotplug not supported

Seats added or removed after the daemon starts are not detected, and neither
is a monitor being connected to an existing seat. atrium enumerates seats once
at startup and holds a static list for the lifetime of the process. Tracked in
[#28](https://github.com/kavau/atrium/issues/28).

### Compile-time configuration only

All settings live in `src/config.h` and are baked in at build time. There is
no runtime config file.

### No display-connected check

atrium does not check whether a display is connected before spawning a greeter.
Seats with a GPU but no monitor will crash-loop the cage compositor
indefinitely. As a workaround, add the offending seat to `CONFIG_IGNORE_SEATS`
in `src/session.h`.

### Greeter SIGKILL escalation not implemented

When tearing down a session, atrium sends `SIGTERM` to cage and waits a fixed
delay before assuming it has exited. There is no `SIGKILL` follow-up if cage
ignores `SIGTERM`. Tracked in [#3](https://github.com/kavau/atrium/issues/3).

### Compositor selection is global, not per-user

`CONFIG_COMPOSITOR` selects a single compositor for every seat and every user.
Per-user session selection (e.g. from a `.desktop` file) is not supported.
Tracked in [#24](https://github.com/kavau/atrium/issues/24).

### PAM session opened in child process

`pam_open_session` is called inside the child process that execs the
compositor, so the PAM session's kernel audit pid does not match the daemon pid
that ran `pam_authenticate`. Some PAM modules (e.g. `pam_loginuid`) may log a
warning or behave unexpectedly. Tracked in
[#42](https://github.com/kavau/atrium/issues/42).

### Passwordless users bypass PAM

Users listed in `CONFIG_PASSWORDLESS_USERS` skip `pam_authenticate` entirely
and proceed directly to session creation. There is no PAM account/session
check for these users. Tracked in
[#30](https://github.com/kavau/atrium/issues/30).

### Some daemon output may not appear in the journal

The daemon's stderr is not routed through `sd_journal_stream_fd`, so error
output written directly to stderr (e.g. from child processes) may not appear
in `journalctl -u atrium`. If you see unexpected behaviour with no journal
output, check `dmesg` or run atrium under `systemd-run` with `--pty`. Tracked
in [#54](https://github.com/kavau/atrium/issues/54).

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

---

## Community

- **GitHub Discussions** — questions, ideas, and atrium-specific topics: [github.com/kavau/atrium/discussions](https://github.com/kavau/atrium/discussions)
- **r/linux_multiseat** — general Linux multiseat discussion: [reddit.com/r/linux_multiseat](https://www.reddit.com/r/linux_multiseat/)

## Reporting Issues

Bug reports and feature requests are welcome. Please open an issue on
[GitHub](https://github.com/kavau/atrium/issues) and include:

- A description of the problem or request.
- Relevant journal output (`sudo journalctl -u atrium -b`).
- Your distro, kernel version, and hardware configuration (especially for
  multiseat-related issues).
