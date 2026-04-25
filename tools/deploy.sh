#!/bin/bash
# deploy.sh — sync source to a remote machine and build atrium there.
#
# Usage: ./deploy.sh user@host:/path/to/dest [-d DISTRO] [-i] [-r]
#
#   user@host:/path/to/dest   Remote target in rsync format.
#                             The path portion defaults to ~/atrium if omitted.
#   -d DISTRO                 PAM config to install: arch, debian, or fedora (default: arch)
#   -i                        Install after building (sudo ninja -C build install)
#   -r                        Restart the atrium systemd unit after installing (implies -i)

set -euo pipefail

DISTRO=arch
INSTALL=0
RESTART=0

usage() {
    grep '^#' "$0" | grep -v '^#!/' | sed 's/^# \{0,1\}//'
    exit 1
}

TARGET="${1:-}"
if [[ -z "$TARGET" || "$TARGET" == -* ]]; then
    echo "error: target is required as the first argument" >&2
    usage
fi
shift

while getopts ":d:ir" opt; do
    case $opt in
        d) DISTRO="$OPTARG" ;;
        i) INSTALL=1 ;;
        r) INSTALL=1; RESTART=1 ;;
        *) usage ;;
    esac
done

# Split "user@host:/path" into HOST and DEST.
# If no colon is present, use the default destination.
if [[ "$TARGET" == *:* ]]; then
    HOST="${TARGET%%:*}"
    DEST="${TARGET#*:}"
else
    HOST="$TARGET"
    DEST="~/atrium"
fi

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "==> Syncing source to $HOST:$DEST"
rsync -az --delete \
    --exclude='.git/' \
    --exclude='build/' \
    --exclude='compile_commands.json' \
    --exclude='tmp/' \
    "$SCRIPT_DIR/" \
    "$HOST:$DEST/"

echo "==> Building on $HOST"
ssh "$HOST" bash -l <<EOF
set -euo pipefail
cd $DEST
if [[ ! -d build ]]; then
    echo "--- Running meson setup"
    meson setup build -Dpam_config=$DISTRO -Dsysconfdir=/etc
else
    echo "--- Reconfiguring meson"
    meson setup build --reconfigure -Dpam_config=$DISTRO -Dsysconfdir=/etc
fi
ninja -C build
EOF

if [[ $INSTALL -eq 1 ]]; then
    if [[ $RESTART -eq 1 ]]; then
        echo "==> Installing and restarting on $HOST"
        ssh -tt "$HOST" "cd $DEST && sudo ninja -C build install && sudo systemctl restart atrium"
    else
        echo "==> Installing on $HOST"
        ssh -tt "$HOST" "cd $DEST && sudo ninja -C build install"
    fi
fi

echo "==> Done"
