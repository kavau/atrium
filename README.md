# atrium

A lightweight display manager for Linux with first-class multiseat support.

## Dependencies

- `meson`, `ninja`: build system

## Build and Installation

```shell
meson setup build
ninja -C build
sudo ninja -C build install
```

## Development

To copy the source to a remote machine and build remotely:

```shell
./tools/deploy.sh
```

To test atrium from within a user session:

```shell
sudo systemd-run --scope build/atrium-dev
```

`systemd-run --scope` is needed in order to run atrium in a fresh scope, not in
the scope of the existing session.

To install only the daemon or only the greeter, use

```shell
sudo meson install -C build --tags daemon
sudo meson install -C build --tags greeter
```
