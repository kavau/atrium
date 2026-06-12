# atrium

## Development

### Remote deployment

To copy the source to a remote machine and build remotely, set `USER` and `HOST`
in the script to the appropriate values, then:

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
