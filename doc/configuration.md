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
  
### Greeter background and themes

Set `background-image` in `/etc/atrium-greeter.conf` to an image path, or to a
directory to pick a random image on each launch. Available formats are JPEG and
PNG (always), and WebP, GIF, BMP, SVG, TIFF (on most systems).

Set `theme` to a CSS file to change the color scheme and widget styling. Several themes ship with atrium and are installed under /usr/local/share/atrium/themes/, or you can write your own.

### Session discovery and compositor override

By default, atrium reads session `.desktop` files from
`/usr/share/wayland-sessions/` and `/usr/local/share/wayland-sessions/` at
greeter launch and presents them in a dropdown.

To bypass discovery and always launch a specific compositor, set `compositor=`
and `desktop=` in `/etc/atrium.conf`. When set, the session dropdown is hidden
and the specified command is used for every login on every seat.

X11 sessions (`/usr/share/xsessions/`) are not supported.

### Wallet and keyring auto-unlock

atrium's PAM configuration already includes optional entries for KWallet
and GNOME Keyring. If the corresponding packages are installed, the wallet
or keyring is automatically unlocked at login using the login password.

| Desktop | Package (Arch) | Package (Debian/Ubuntu) | Package (Fedora) |
| --- | --- | --- | --- |
| KDE (KWallet) | `kwallet-pam` | `libpam-kwallet5` | `pam-kwallet` |
| GNOME / COSMIC (GNOME Keyring) | `gnome-keyring` | `libpam-gnome-keyring` | `gnome-keyring` |
