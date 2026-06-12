# atrium

## When Things Go Wrong

If atrium fails to start or you can't log in, switch back to your previous
display manager from a TTY (`Ctrl+Alt+F3`, log in as root or with `sudo`):

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
journalctl -u atrium -b    # logs from the current boot
journalctl -u atrium -b-1  # logs from the previous boot
```

## Common Problems

### The greeter immediately comes back after login

The user session failed to start. To see what went wrong, look up the compositor PID from the atrium log, then query the logs for it:

```sh
journalctl -u atrium -b | grep "started user session"
# note the (PID NNNN) value, then:
journalctl -b _PID=NNNN
```

Alternatively query by time window:

```sh
journalctl -b --since="HH:MM:SS" --until="HH:MM:SS"
```

### My desktop environment does not appear in the greeter's session list

atrium reads `.desktop` files from `/usr/share/wayland-sessions/` and `/usr/local/share/wayland-sessions/` at greeter launch. If a session is missing from the list, check for these common causes:

- No `.desktop` file exists in either directory. Note that atrium does not scan `/usr/share/xsessions/` or `/usr/local/share/xsessions/`.
- The desktop file contains `Hidden=true` or `NoDisplay=true`.
- The `TryExec=` binary was not found on `$PATH` - atrium skips the entry when the binary is absent.

To add a custom session, create a `.desktop` file in
`/usr/local/share/wayland-sessions/`:

```ini
[Desktop Entry]
Name=My Session
Exec=/usr/local/bin/my-compositor
TryExec=/usr/local/bin/my-compositor
Type=Application
```

### GNOME: "Screen Lock disabled / Screen Locking requires the GNOME display manager"

This is due to the deep integration between GDM and the GNOME shell, which
depends the `org.gnome.DisplayManager` service for screen unlocking. When GDM is
not running, GNOME Shell disables its built-in lock screen and shows this
warning. No display manager other than GDM implements this interface, so this
warning will appear with any other display manager.

Tracked in [#91](https://github.com/kavau/atrium/issues/91).

### Multiseat: my computer goes to sleep unexpectedly

Many desktop environments are pre-configured to put the computer in sleep mode after a period of inactivity. The problem is that each seat manages its own idle state independently. If a seat is unused after login, the desktop environment may trigger a system-wide sleep even if another seat is still in active use. To the active seat this appears as a sudden, unexpected sleep.

To fix this, disable the automatic sleep timeout in your desktop environment's power settings.

- In GNOME: Settings > Power > Automatic Suspend
- In KDE Plasma: System Settings > Power Management > Suspend Session
