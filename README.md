# atrium

A Wayland display manager for Linux with first-class multiseat support.

See [doc/architecture.md](doc/architecture.md) for a detailed design overview.

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

## Building

```sh
meson setup build
ninja -C build
```

The compiled daemon binary lands at `build/src/atrium`.

---

## Running

atrium is intended to run as a systemd service (as root). For development, it
can be invoked directly:

```sh
sudo ./build/src/atrium
```

> **Note:** Full functionality requires a running logind instance and appropriate
> udev seat configuration. Running atrium on a machine already in a graphical
> session is not recommended.

---

## Development Testing (Phase 5 — Headless Session Launch)

Phase 5 launches a hardcoded compositor (`sway`) as a hardcoded user
(`testuser`) on each seat. The following steps describe how to test this on a
CachyOS/Arch system with multiple seats.

### Prerequisites

```sh
# Install sway
sudo pacman -S sway

# Create the session user
sudo useradd -m testuser

# Give testuser a sway config (avoids warnings about missing config)
sudo mkdir -p /home/testuser/.config/sway
sudo cp /etc/sway/config /home/testuser/.config/sway/config
sudo chown -R testuser:testuser /home/testuser/.config
```

### Running

atrium must run outside any existing logind session cgroup or it will fail with
"Already running in a session or user slice" when calling `CreateSession`. Use
`systemd-run` to place it in a fresh transient service unit:

```sh
# Stop any existing display manager first
sudo systemctl stop lightdm   # or gdm, sddm, etc.

# Launch atrium as a transient systemd service (output goes to journal)
# If re-running after a previous test, reset the unit first:
#   sudo systemctl reset-failed atrium-test
sudo systemd-run --unit=atrium-test /path/to/build/src/atrium

# Follow the logs
sudo journalctl -u atrium-test.service -f

# Stop atrium
sudo systemctl stop atrium-test
```

### What to verify

- sway launches on each non-seat0 seat (seat0 requires Phase 10 — running as
  the system display manager service before any login session exists).
- Exiting sway (default: `Mod+Shift+E`) causes atrium to restart it after
  ~500 ms and log `compositor exited, restarting in 500 ms`.
- `loginctl list-sessions` shows an active session with `DESKTOP=atrium-dev`
  for each seat where sway is running.
- Sending `SIGTERM` to atrium (or `sudo systemctl stop atrium-test`) shuts down
  cleanly: sway is killed and the logind session is closed.

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
