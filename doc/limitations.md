# atrium

## Known Limitations

### Mixed-GPU setups are untested

Running two different GPU drivers simultaneously (e.g. for AMD + NVIDIA GPUs) is
generally considered unsupported territory on Linux, and can result in driver
instabilities and configuration headaches. If possible, use GPUs that utilize
the same driver.

### Cross-seat keyboard input leakage to text VTs

The Linux kernel routes keystrokes to whichever VT (Virtual Terminal) is in the
foreground system-wide, ignoring udev seat tagging. On a multiseat machine,
keystrokes typed on seat1's keyboard can land on a text login (e.g. agetty) on
seat0 if that VT is in the foreground at the time. This is universal Linux
multiseat behavior, not atrium-specific. Tracked in
[#72](https://github.com/kavau/atrium/issues/72).

### PAM authentication is limited to a single password prompt

The greeter collects a username and password and sends both to the daemon,
rather than letting PAM's conversation function drive the auth flow. As a
result, any PAM stack that emits more than one prompt (e.g. for multi-factor,
hardware tokens, fingerprint, ...) will misbehave. Tracked in
[#93](https://github.com/kavau/atrium/issues/93).

### Fedora SELinux xdm_t workaround

On Fedora, the atrium binary is labelled `xdm_exec_t` during installation, so it
runs in GDM's `xdm_t` domain. The `atrium-local` policy module grants `xdm_t`
the permissions needed to run `udevadm settle`, connect to the udev socket, stop
`getty@.service` on VT handoff, and query systemd unit status.

If any new AVCs appear after a Fedora policy update, re-run `ninja install` to
rebuild and reinstall the module.

The user compositor and all processes in the user session also inherit the
`xdm_t` domain because atrium execs the compositor without a SELinux domain
transition. This is a known security gap tracked in
[#105](https://github.com/kavau/atrium/issues/105).

As a long-term solution, a dedicated SELinux policy module with its own
`atrium_t` domain is tracked in
[#86](https://github.com/kavau/atrium/issues/86).

