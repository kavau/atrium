# atrium

A lightweight display manager for Linux with first-class multiseat support.

## Dependencies

- `meson`, `ninja`: build system

## Build and Installation

```shell
meson setup build
ninja -C build
```

## Development

To test atrium from within a user session:

```shell
sudo systemd-run --scope build/atrium
```
