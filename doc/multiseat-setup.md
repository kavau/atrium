# Multiseat Setup Guide

This guide covers how to set up a Linux multiseat system using `loginctl` and
atrium. It assumes you already have atrium installed (see [README](../README.md)
for build and installation instructions).

For questions and discussion, see
[r/linux_multiseat](https://www.reddit.com/r/linux_multiseat/).

## Overview

In a multiseat setup, a single machine serves multiple independent users, each
with their own monitor, keyboard, and mouse. The Linux kernel and systemd/logind
provide full support for this: each set of devices is grouped into a **seat**,
and each seat gets its own independent login session.

Each seat needs its own graphics device: an integrated APU, a discrete GPU, or a
USB graphics adapter will work. Preferably all devices use the same graphics
driver (e.g. all Intel, all AMD, or all NVIDIA). A single GPU with multiple
outputs cannot be split across seats; the entire GPU belongs to one seat.

In principle, setting up an additional seat is as easy as running

```console
loginctl attach seat1 /sys/path/to/device
```

for each device (GPU, keyboard, mouse) you want on that seat. The difficulty
lies in identifying the correct device path. The rest of this guide will walk
you through it.

Every device not explicitly assigned to another seat belongs to the default
seat, **seat0**. Logind writes persistent udev rules under `/etc/udev/rules.d/`
so seat assignments survive across reboots.

A useful guideline that keeps the configuration simple is: **attach parent
devices rather than individual child devices.** For example, when you attach a
USB hub to a seat, all devices plugged into it will belong to that seat as well.

## Prerequisites

- systemd with logind (any modern mainstream distribution)
- Two or more GPUs (discrete, integrated, or USB DisplayLink)
- Separate keyboard and mouse per seat
- Root access

## Step 1: attach a GPU to the new seat(s)

List all GPUs and verify they use the same kernel driver:

```console
$ lspci -nnk | grep -A 3 -E "VGA|3D|Display"
```

Typical output:

```
01:00.0 VGA compatible controller: Advanced Micro Devices ... Navi 48 ...
    Kernel driver in use: amdgpu
09:00.0 VGA compatible controller: Advanced Micro Devices ... Navi 22 ...
    Kernel driver in use: amdgpu
```

> **Tip:** Prefer GPUs that use the same kernel driver (e.g. two AMD cards both
> using `amdgpu`). Mixing GPU vendors (e.g. NVIDIA + AMD) loads multiple DRM
> drivers simultaneously, which is poorly tested and can cause conflicts.

The PCI address (e.g. `01:00.0`, `09:00.0`) identifies each GPU. To map these
to DRM card numbers (`card0`, `card1`, ...), check the symlinks:

```console
$ ls -l /sys/class/drm/card*/device
```

Each `device` symlink target contains the PCI address, e.g.:

```
/sys/class/drm/card0/device -> ../../../0000:09:00.0
/sys/class/drm/card1/device -> ../../../0000:01:00.0
```

This tells us that `card0` is the GPU at PCI `09:00.0` and `card1` is at `01:00.0`.

Now we use `loginctl attach` to assign a GPU to the new seat. The seat is
created automatically when a 'master-of-seat' (that is, graphics device) is
assigned to it.

```console
$ sudo loginctl attach seat1 /sys/class/drm/card1
```

## Step 2: attach input devices

The simplest approach is to plug all peripherals for the new seat into a
**single USB hub** (either an onboard USB hub or an external hub) and assign
that hub to the seat. This way one `loginctl attach` covers all input (and
audio) devices at once.

Alternatively, the USB ports into which the devices are plugged can be assigned
individually. In either case we need the sysfs path of the device. Use whichever
of the following two methods suits you:

### Method 1: udevadm monitor (recommended)

Run `udevadm monitor` in a terminal, then plug (or unplug) any device into the
hub or port you want to identify. `udevadm monitor` then prints the full device
path; the hub or port path is a prefix of the full path. For example:

```console
$ sudo udevadm monitor --udev
UDEV  [1234.567] add  /devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3.1 (usb)
...
```

Reading the path from left to right:

```text
/devices/pci0000:00/0000:00:14.0                - PCI USB controller (cannot be attached)
/devices/pci0000:00/0000:00:14.0/usb1           - root hub (all ports on this controller)
/devices/pci0000:00/0000:00:14.0/usb1/1-3       - port 3 of the root hub (back-panel port 3)
/devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3.1 - port 1 of the device (external hub) at 1-3
```

Choose the level that fits your setup:

- **Root hub (`usb1`)** - assigns every port on the controller to the seat. Use
  this when the entire controller is dedicated to seat1 (e.g. an add-in USB
  controller card, or if your motherboard has multiple controllers).
- **Port (`1-3`)** - assigns the device at back-panel port 3 and everything
  downstream. If an external hub is plugged in there, all hub ports follow.
- **Single device (`1-3.1`)** — assigns just one port of the plugged-in device.

Prepend `/sys` before passing any of these paths to `loginctl attach`.

### Method 2: `loginctl seat-status seat0`

`loginctl seat-status seat0` lists all devices on seat0 with their full sysfs
paths — scan it to find your hub or input device:

```console
$ loginctl seat-status seat0
```

For example, the following snippet shows a USB mouse on root hub `usb7`,
downstream of port `7-1`:

```text
  └─/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1
    usb:7-1
    └─/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1/7-1.2
      usb:7-1.2
      └─.../input/input9 "USB Optical Mouse"
```

To attach the root hub (and all its devices), use the `usb7` path:
`/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7`.

To attach only the port that has the mouse attached, include the `7-1` segment:
`/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1`.

### Attach devices

Now that we have the sysfs path, attach the identified device(s) to the seat, e.g.:

```console
$ sudo loginctl attach seat1 /sys/devices/pci0000:00/0000:00:14.0/usb1
```

If you want to attach multiple input devices, repeat this command for each
device. Only devices you want on **non-default** seats need to be assigned.
Everything else stays on seat0 automatically.

Verify the assignments took effect:

```console
$ loginctl list-seats
$ loginctl seat-status seat0
$ loginctl seat-status seat1
```

You should see two seats - seat1 should now show the GPU and USB hub (or
individual input devices) you just assigned, while seat0 should show all other
devices.

## Step 3: Activate atrium

If atrium is installed but not yet enabled:

```console
$ sudo systemctl disable gdm   # or sddm, lightdm, etc.
$ sudo systemctl enable atrium
```

Then reboot. atrium will discover all seats and launch a greeter on each one.

## Removing Seat Assignments

To remove all custom seat assignments and return to a single-seat setup:

```console
$ sudo loginctl flush-devices
```

To remove a specific device from a non-default seat (returns it to seat0):

```console
$ sudo loginctl attach seat0 /sys/devices/pci0000:00/0000:01:00.0
```

> **Note:** If input devices don't appear on the expected seat after a
> reassignment, restart atrium (or reboot) to let the greeter pick up the
> updated layout.

## References

- [loginctl(1)](https://www.freedesktop.org/software/systemd/man/loginctl.html)
- [systemd Multi-Seat](https://www.freedesktop.org/wiki/Software/systemd/multiseat/)
- [Debian Multi-Seat HOWTO](https://wiki.debian.org/Multi_Seat_Debian_HOWTO) -
  mostly covers legacy X11 setups, but the "Loginctl" section is applicable
