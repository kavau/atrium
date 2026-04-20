---
Created: April 20, 2026
Last Updated: April 20, 2026
---

# Multiseat Setup Guide

This guide covers how to set up a Linux multiseat system using `loginctl` and
atrium. It assumes you already have atrium installed (see [README](../README.md)
for build and installation instructions).

## Overview

In a multiseat setup, a single machine serves multiple independent users, each
with their own monitor, keyboard, and mouse. The Linux kernel and systemd/logind
provide full support for this: each set of devices is grouped into a **seat**,
and each seat gets its own independent login session.

Each seat needs its own graphics device — a discrete GPU, an integrated APU, or
a USB DisplayLink adapter, preferably all using the same graphics driver (e.g.
all Intel, all AMD, or all NVIDIA). A single GPU with multiple outputs cannot be
split across seats; the entire GPU belongs to one seat.

In principle, setting up an additional seat is as easy as running
`loginctl attach seat1 /path/to/device` for each device (GPU, keyboard, mouse,
etc.) you want on that seat. The difficulty lies in identifying the correct
device path — that's what most of this guide walks you through.

Every device not explicitly assigned to another seat belongs to **seat0** — the
default seat. Logind writes persistent udev rules under `/etc/udev/rules.d/`
so seat assignments survive across reboots.

A useful principle for keeping the configuration simple: **attach parent devices
rather than individual child devices.** When you attach a PCI GPU to a seat, all
its DRM and framebuffer nodes follow. When you attach a USB controller, all
keyboards, mice, and other USB devices plugged into its ports follow. That said,
you can also attach individual devices directly if needed (see Step 4).

## Prerequisites

- systemd with logind (any modern distribution)
- Two or more GPUs (discrete, integrated, or USB DisplayLink)
- Separate keyboard and mouse per seat (or a USB hub per seat)
- Root access

## Step 1: Identify Your Hardware

### GPUs

List all GPUs and verify they use the same kernel driver:

```console
$ lspci -nnk | grep -A 3 VGA
```

Typical output:

```
01:00.0 VGA compatible controller: Advanced Micro Devices ... Navi 48 ...
    Kernel driver in use: amdgpu
09:00.0 VGA compatible controller: Advanced Micro Devices ... Navi 22 ...
    Kernel driver in use: amdgpu
```

The PCI address (e.g. `01:00.0`, `09:00.0`) identifies each GPU. To map these
to DRM card numbers (`card0`, `card1`, ...), check the symlinks:

```console
$ ls -l /sys/class/drm/card*/device
```

Each symlink target contains the PCI address, e.g.:

```
/sys/class/drm/card0/device -> ../../../0000:09:00.0
/sys/class/drm/card1/device -> ../../../0000:01:00.0
```

This tells you `card0` is the GPU at PCI `09:00.0` and `card1` is at `01:00.0`.

> **Note:** DRM card numbering is determined by driver probe order and is
> typically stable for unchanged hardware. In any case, `loginctl attach`
> persists seat assignments by PCI hardware path (`ID_FOR_SEAT`), so
> assignments remain correct even if card numbering were to change.

### USB Controllers and Input Devices

The simplest approach is to plug all second-seat peripherals (keyboard, mouse,
USB audio adapter, etc.) into a **single USB hub** and assign that hub to the
seat. This way one `loginctl attach` command covers all input and audio devices
at once.

You'll need the sysfs path of the hub or controller you want to assign. Use
whichever of the following methods suits your situation.

#### Method 1: udevadm monitor (plug in a device)

Run `udevadm monitor` in one terminal, then plug a device into the hub you want
to identify — the path is printed immediately:

```console
$ sudo udevadm monitor --udev
UDEV  [1234.567] add  /devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3.1 (usb)
...
```

Reading the path from left to right:

```text
/devices/pci0000:00/0000:00:14.0                — PCI USB controller
/devices/pci0000:00/0000:00:14.0/usb1           — root hub (bus 1)
/devices/pci0000:00/0000:00:14.0/usb1/1-3       — hub on port 3
/devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3.1 — device on hub port 1
```

You can attach at any level: the controller, a hub, or a single device.
Attaching a parent also assigns all its children to that seat. Note that
you always need to prepend the `/sys` mountpoint prefix to the path before
using.

#### Method 2: loginctl seat-status (inspect without replugging)

If devices are already plugged in, `loginctl seat-status seat0` lists all
devices currently on seat0 with their full sysfs paths — scan it to identify
your hub or controller:

```console
$ loginctl seat-status seat0
```

For example, the following output snippet shows a USB mouse on hub `7-1` on
bus 7, under PCI USB controller `0000:15:00.0` (itself behind a PCIe bridge at
`0000:00:08.3`):

```text
  ├─/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7
  │ usb:usb7
  │ └─/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1
  │   usb:7-1
  │   ├─/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1/7-1.2
  │   │ usb:7-1.2
  │   │ ├─/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1/7-1.2/7-1.2:1.0/0003:1BCF:0005.0005/hidraw/hidraw4
  │   │ │ hidraw:hidraw4
  │   │ └─/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1/7-1.2/7-1.2:1.0/0003:1BCF:0005.0005/input/input9
  │   │   input:input9 "USB Optical Mouse"
```

To attach only the mouse, use the device path
`/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1/7-1.2`. To attach
the entire hub `7-1` (and all downstream devices), use
`/sys/devices/pci0000:00/0000:00:08.3/0000:15:00.0/usb7/7-1`.

#### Method 3: lspci (for dedicated USB controller cards)

If you have a dedicated add-in USB controller card, you can identify it by PCI
address and look up the sysfs path in Step 3:

```console
$ lspci | grep -i USB
00:14.0 USB controller: Intel Corporation 200 Series/Z370 Chipset Family USB 3.0 xHCI Controller
03:00.0 USB controller: Renesas Technology Corp. uPD720201 USB 3.0 Host Controller
```

If you only have one USB controller, assign individual USB devices or hubs directly
— use `udevadm monitor` to find each device's sysfs path.

### Sound Cards

List audio devices:

```console
$ aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: PCH [HDA Intel PCH], device 0: ALC892 Analog [ALC892 Analog]
  Subdevices: 1/1
card 1: HDMI [HDA ATI HDMI], device 3: HDMI 0 [HDMI 0]
  Subdevices: 1/1
```

To find the sysfs path for a sound card, use `/dev/snd/controlCX` where X is
the card number from `aplay -l`:

```console
$ udevadm info -q path -n /dev/snd/controlC1
/devices/pci0000:00/0000:01:00.1/sound/card1  # prepend /sys before using
```

## Step 2: Plan Your Seat Assignments

Only devices you want on **non-default seats** need to be assigned. Everything
else stays on seat0 automatically.

For each additional seat, you need at minimum:
- **One GPU** (or one DisplayLink adapter) — cannot be shared with another seat
- **Keyboard and mouse** — either on a dedicated USB controller, or assigned
  individually by device path

Optional but recommended:
- **One sound card** for audio on that seat (a USB audio adapter plugged into
  the seat's USB hub is the simplest option — it follows the controller
  assignment automatically)

### Common Setups

- **Two discrete GPUs:** Assign each GPU and a separate USB controller (or hub)
  to each seat. Add a dedicated sound card or USB audio adapter for the second
  seat if needed.
- **Integrated APU + discrete GPU:** Use the integrated GPU for seat0 and the
  discrete GPU for seat1. Split the onboard USB controllers (or use a hub)
  between seats.
- **GPU + USB DisplayLink adapter:** Requires the `udl` or `evdi` driver.
  DisplayLink adapters have inherent bandwidth limitations — research Linux
  support for your specific model before committing to this approach.

> **Tip:** Prefer GPUs that use the same kernel driver (e.g. two AMD cards both
> using `amdgpu`). Mixing GPU vendors (e.g. NVIDIA + AMD) loads multiple DRM
> drivers simultaneously, which is poorly tested and can cause conflicts.
> Verify all GPUs use the same driver with `lspci -nnk | grep -A 3 VGA`.

## Step 3: Attach Devices to Seats

Use `loginctl attach` to assign each device to a seat. The seat is created
automatically when the first device is assigned to it.

**GPU** — use the DRM card path from Step 1:

```console
$ sudo loginctl attach seat1 /sys/class/drm/card1
```

**USB controller** — find the sysfs path from the PCI address shown by `lspci`:

```console
$ find /sys/devices -name '0000:03:00.0' -maxdepth 4 2>/dev/null
/sys/devices/pci0000:00/0000:03:00.0
$ sudo loginctl attach seat1 /sys/devices/pci0000:00/0000:03:00.0
```

Alternatively, plug a device into the controller and use `udevadm monitor` (as
in Step 1) — the printed path contains the controller's sysfs path as a prefix.

**Individual USB devices or hubs** (if not assigning a whole controller) — use
the path from `udevadm monitor` (plug in the device and read the path printed)
or from `loginctl seat-status seat0` (if the device is already plugged in). See
Step 1 for both methods.

```console
$ sudo loginctl attach seat1 /sys/devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3.1
```

**Sound card** (optional):

```console
$ udevadm info -q path -n /dev/snd/controlC1
/devices/pci0000:00/0000:00:1f.3/sound/card1
$ sudo loginctl attach seat1 /sys/devices/pci0000:00/0000:00:1f.3
```

(Prepend `/sys` to the `udevadm` output; attach the parent PCI device, not the `sound/` child.)

> **Tip:** If your sound card is a USB audio adapter plugged into the hub
> you're assigning to seat1, it will follow the controller assignment
> automatically — no separate attach needed.

Verify the assignments took effect:

```console
$ loginctl list-seats
$ loginctl seat-status seat0
$ loginctl seat-status seat1
```

`seat1` should now show the GPU, USB controller, and (if assigned) sound card.
Keyboards and mice plugged into the assigned USB controller's ports should also appear
under `seat1`.

For systems with more than two seats, repeat the process above for `seat2`, `seat3`, etc.

## Step 4: Activate atrium

If atrium is installed but not yet enabled:

```console
$ sudo systemctl disable gdm   # or sddm, lightdm, etc.
$ sudo systemctl enable atrium
```

Then reboot. atrium will discover all seats and launch a greeter on each one.

> See the [README](../README.md) for installation and build instructions.

## Removing Seat Assignments

To remove all custom seat assignments and return to a single-seat setup:

```console
$ sudo loginctl flush-devices
```

To remove a specific device from a non-default seat (returns it to seat0):

```console
$ sudo loginctl attach seat0 /sys/devices/pci0000:00/0000:01:00.0
```

## References

- [loginctl(1)](https://www.freedesktop.org/software/systemd/man/loginctl.html)
- [systemd Multi-Seat](https://www.freedesktop.org/wiki/Software/systemd/multiseat/)
- [Debian Multi-Seat HOWTO](https://wiki.debian.org/Multi_Seat_Debian_HOWTO) —
  mostly covers legacy X11 setups, but the "Loginctl" section is applicable
