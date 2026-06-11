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

For instructions how to **configure your hardware for multiseat**,
see [doc/multiseat-setup.md](doc/multiseat-setup.md).

### Status

**v0.3 - fully functional but still in early development.**

atrium runs well as a daily-driver display manager. The core workflow (seat
discovery, user authentication, session lifecycle management) is fully
operational. That said, atrium has been tested on a limited range of hardware
and distributions, so expect some rough edges.

See [doc/architecture.md](doc/architecture.md) for a **detailed design overview**.

#### What' new since v0.2

- **Runtime config files** - `/etc/atrium.conf` and `/etc/atrium-greeter.conf`
  replace the compile-time `src/defs.h`.
- **Session discovery** - atrium scans `/usr/share/wayland-sessions/` and
  `/usr/local/share/wayland-sessions/` for desktop sessions; the `compositor=`
  and `desktop=` settings are now optional overrides. The greeter shows a
  dropdown of discovered sessions (if there is more than one and no override is
  set).
- **Wallet/keyring auto-unlock** - KWallet and GNOME Keyring are unlocked
  automatically at login if the relevant PAM module packages are installed.

### Supported Distros

| Distro | Status |
| --- | --- |
| Arch / CachyOS | Tested |
| Debian / Ubuntu | Tested |
| Fedora | Tested (wallet/keyring auto-unlock currently not working) |

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
sudo dnf install systemd-devel pam-devel gtk4-devel cage gcc meson ninja-build \
    policycoreutils-python-utils checkpolicy
```

(`policycoreutils-python-utils` and `checkpolicy` are needed to set up the
SELinux contexts and policy module.)

### 2. Build and install

```shell
meson setup build -Ddist=<your-distro>   # arch, debian, fedora
ninja -C build
sudo ninja -C build install
```

The `-Ddist` option (required) selects the correct PAM stack for the target distribution. Possible values for `dist` are: `arch` (for Arch/CachyOS), `debian` (for Debian/Ubuntu), or `fedora` (for Fedora).

### 3. Configure

> This step can usually be skipped - the defaults work for a standard
> single-seat or multiseat setup.

- **`/etc/atrium.conf`** — daemon settings: greeter command, ignored seats,
  optional compositor override, among others.
- **`/etc/atrium-greeter.conf`** — greeter UI settings: idle blanking timeout,
  background image, theming etc.

Each config file is heavily commented; consult them for the full set of
available keys and their meaning. Missing config files or keys fall back onto
compiled-in defaults.

### 4. Enable and start

> **Multiseat setups:** seat assignment must be configured with `loginctl
> attach` before starting atrium. Without this step only a single seat exists.
> See [doc/multiseat-setup.md](doc/multiseat-setup.md) for a step-by-step guide.

Disable the current display manager, and enable atrium:

```sh
sudo systemctl disable gdm   # substitute your current display manager
sudo systemctl enable atrium
```

Then reboot. atrium will start on boot and launch a greeter on every seat.

---

## Further Reading

- [Multiseat Setup Guide](doc/multiseat-setup.md)
- [Configuration](doc/configuration.md)
- [Architecture](doc/architecture.md)
- [Development](doc/development.md)

## Community

- **GitHub Discussions** - questions, ideas, and atrium-specific topics: [github.com/kavau/atrium/discussions](https://github.com/kavau/atrium/discussions)
- **r/linux_multiseat** - general Linux multiseat discussion: [reddit.com/r/linux_multiseat](https://www.reddit.com/r/linux_multiseat/)

## Reporting Issues

Bug reports and feature requests are welcome. Please open an issue on
[GitHub](https://github.com/kavau/atrium/issues) and include:

- A description of the problem or request.
- Relevant journal output (`sudo journalctl -u atrium -b`).
- Your distro, kernel version, and hardware configuration (incl. graphics
  drivers; especially for multiseat-related issues).
