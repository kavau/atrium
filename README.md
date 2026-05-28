# atrium

A lightweight display manager, built for Linux multiseat setups. Discovers seats
via logind, shows a greeter on each seat, handles user authentication, and hands
off to an independent user session per seat.

<!-- markdownlint-disable MD001 -->
### Why atrium?
<!-- markdownlint-enable MD001 -->

The Linux kernel and low-level system stack have had solid multiseat support for
years: udev handles device assignment, logind manages independent user sessions
per seat, and Wayland compositors work with whatever devices logind gives them.
The weak link has always been the display manager. Existing ones usually treat
multiseat as an afterthought, with implementations that are brittle and
difficult to get working reliably.

atrium is designed around multiseat from the start, focusing on correct seat
discovery, VT handling, and isolated session management. The project targets a
modern Linux stack using systemd/logind, PAM, and a Wayland graphical
environment. The lack of historical baggage keeps atrium's code base lean and
tractable.

<!-- markdownlint-disable MD001 -->
### What is multiseat?
<!-- markdownlint-enable MD001 -->

A multiseat setup allows multiple users to work on a single computer at the same
time. By connecting multiple monitors, keyboards, and mice, each user gets their
own separate desktop and a fully isolated user session. Great for co-working or
multiplayer gaming. Each seat requires its own GPU (integrated graphics, a
discrete card, or a USB graphics adapter all work).

### Status

**v0.2 - fully functional but still in early development.**

atrium runs well as a daily-driver display manager. The core workflow (seat
discovery, user authentication, session lifecycle management) is fully
operational. That said, atrium has been tested on a limited range of hardware
and distributions, so expect some rough edges.

### Supported Distros

| Distro | Status |
| --- | --- |
| Arch / CachyOS | Tested |
| Debian / Ubuntu | Tested |
| Fedora | PAM config supplied but yet untested |

Other systemd-based distros should work - the only distro-specific piece is the
PAM stack. Adapt one of the provided PAM configs as needed.

---

## Installation

### 1. Install dependencies

- `libsystemd` - logind session management
- `libudev` - seat discovery
- `libpam` - user authentication
- `gtk4` - greeter UI
- `cage` - Wayland compositor hosting the greeter
- `meson`, `ninja` - build system

On Debian/Ubuntu:

```sh
sudo apt install libsystemd-dev libudev-dev libpam0g-dev libgtk-4-dev cage meson ninja-build
```

On Arch/CachyOS:

```sh
sudo pacman -S systemd pam gtk4 cage meson ninja
```

On Fedora:

```sh
sudo dnf install systemd-devel pam-devel gtk4-devel cage gcc meson ninja-build
```

### 2. Configure

> This step can usually be skipped - the defaults work for a standard
> single-seat or multiseat setup.

atrium's config is currently hardcoded in lib/defs.h (daemon) and greeter/defs.h
(greeter). If needed, these files must be edited before build and install.

### 3. Build and install

```shell
meson setup build -Ddist=<your-distro>   # arch, debian, fedora
ninja -C build
sudo ninja -C build install
```

The `-Ddist` option (required) selects the correct PAM stack for the target distribution. Possible values for `dist` are: `arch` (for Arch/CachyOS), `debian` (for Debian/Ubuntu), or `fedora` (for Fedora).

### 4. Enable and start

> **Multiseat setups:** seat assignment must be configured with `loginctl attach` before starting atrium - without this step only a single seat exists (TODO: link multiseat setup guide).

Disable the current display manager, and enable atrium:

```sh
sudo systemctl disable gdm   # substitute your current display manager
sudo systemctl enable atrium
```

Then reboot. atrium will start on boot and launch a greeter on every seat.

---

## Development

### Remote deployment

To copy the source to a remote machine and build remotely:

```shell
./tools/deploy.sh
```

### Local testing

To test atrium from within a user session:

```shell
sudo systemd-run --scope build/atrium-dev
```

`systemd-run --scope` is needed to run atrium in a fresh scope, not in
the scope of the existing session.

To install only the daemon or only the greeter, use

```shell
sudo meson install -C build --tags daemon
sudo meson install -C build --tags greeter
```
