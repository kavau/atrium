# atrium

## Configuration

atrium reads two config files under `/etc` at runtime:

- **`/etc/atrium.conf`** — daemon settings: greeter command, ignored seats,
  optional compositor override etc.
- **`/etc/atrium-greeter.conf`** — greeter UI settings: idle blanking timeout,
  background image, theming etc.

Each config file is heavily commented; consult them for the full set of
available keys and their meaning. Missing config files or keys fall back onto
compiled-in defaults.

Note: any configuration changes can be applied live with

```sh
sudo systemctl reload atrium
```

This command reloads the daemon configuration and restarts all idle seats, so
that greeters also pick up their new config.

---

### Shutdown and Reboot from the greeter

By default, the greeter shows "Shut Down" and "Reboot" buttons on the login screen.
Anyone can use them, no credentials are required. On a multiseat machine they take
down any active user session running on another seat.

Set `power-actions` in `/etc/atrium.conf` to `false` to hide the buttons and disable
shutdown/reboot from the greeter.

---

### Greeter background and themes

Set `background-image` in `/etc/atrium-greeter.conf` to an image path, or to a
directory to pick a random image on each launch. Available formats are JPEG and
PNG (always), and WebP, GIF, BMP, SVG, TIFF (on most systems).

Set `theme` to a CSS file to change the color scheme and widget styling. Several
themes ship with atrium and are installed under `/usr/share/atrium/themes/`
(distro packages) or `/usr/local/share/atrium/themes/` (source builds).

To write your own, start from
[`example.css`](https://github.com/kavau/atrium/blob/main/data/themes/example.css)
in that directory. It is a fully annotated reference listing every color
variable and every styleable element. The example theme is close to the built-in
theme in appearance with a few additions (drop shadow, keyboard focus ring). The
configured theme file is appended to atrium's built-in CSS, so you only need to
keep the rules you actually change.

---

### Session discovery and compositor override

By default, atrium reads session `.desktop` files from
`/usr/share/wayland-sessions/` and `/usr/local/share/wayland-sessions/` at
greeter launch and presents them in a dropdown.

To bypass discovery and always launch a specific compositor, set `compositor=`
and `desktop=` in `/etc/atrium.conf`. When set, the session dropdown is hidden
and the specified command is used for every login on every seat.

X11 sessions (`/usr/share/xsessions/`) are not supported.

---

### Wallet and keyring auto-unlock

atrium's PAM configuration already includes optional entries for KWallet
and GNOME Keyring. If the corresponding packages are installed, the wallet
or keyring is automatically unlocked at login using the login password.

| Desktop | Package (Arch) | Package (Debian/Ubuntu) | Package (Fedora) |
| --- | --- | --- | --- |
| KDE (KWallet) | `kwallet-pam` | `libpam-kwallet5` | `pam-kwallet` |
| GNOME / COSMIC (GNOME Keyring) | `gnome-keyring` | `libpam-gnome-keyring` | `gnome-keyring` |

---

### Custom compositor as greeter host

atrium uses `cage`, a minimal kiosk compositor, to launch the greeter. It is the
safest choice, since there is no way to reach the system from the login screen.
If you need configuration options (e.g. tap-to-click, numlock state, key
repeat), `labwc` is an alternative. To enable `labwc` as the greeter host, the
following steps are needed. Note that this is a power user option, and not
officially supported.

1. Install `labwc` (e.g. `pacman -S labwc` or `apt install labwc`, depending on
   your distro)

2. Create a labwc configuration file `/etc/atrium-labwc-greeter.xml` with the
   following content (make sure the file is world-readable).

   `labwc` is not a kiosk compositor, and hence by default will allow actions that
   may pose a security risk in a greeter context (e.g. by default `Super+Return`
   launches a terminal; right-click on the desktop opens a menu). The config file
   suppresses these actions, and hence is essential to keep your system secure.

```xml
<?xml version="1.0"?>
<labwc_config>

  <!-- A single <keybind> entry disables all default keybinds. -->
  <keyboard>
    <numlock>on</numlock>  <!-- Optional: turn numlock on -->
    <keybind key="W-Return"><action name="None" /></keybind>
  </keyboard>

  <!-- A single <mousebind> entry disables all default mousebinds. -->
  <mouse>
    <context name="Desktop">
      <mousebind button="Right" action="Press"><action name="None" /></mousebind>
    </context>
  </mouse>

</labwc_config>
```

3. Add the following line to `/etc/atrium.conf`.

   Adjust the path to your installation: `/usr/lib/atrium/` for distro packages,
   or `/usr/local/lib/atrium/` for source builds.

```text
greeter = /usr/bin/labwc -c /etc/atrium-labwc-greeter.xml -S /usr/lib/atrium/atrium-gtk-greeter
```

4. Reload the config (and restart idle seats) with `sudo systemctl reload atrium`.

   Verify that at the login screen, `Super+Return` does nothing.
