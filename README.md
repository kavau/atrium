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

## Project Layout

```
src/       daemon source (event loop, seat/session/auth/bus/vt/greeter)
greeter/   atrium-greeter: GTK4 login UI
tools/     diagnostic and integration-test binaries
data/      installed config files (PAM stack, systemd unit)
doc/       architecture guide, implementation plan
build/     Meson build output (git-ignored)
```
