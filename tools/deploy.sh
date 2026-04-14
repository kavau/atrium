#!/bin/bash
# deploy.sh — sync source to a remote machine and build atrium there.
#
# Usage: ./deploy.sh user@host:/path/to/dest [-i] [-r]
#
#   user@host:/path/to/dest   Remote target in rsync format.
#                             The path portion defaults to ~/atrium if omitted.
#   -i                        Install after building (sudo ninja -C build install)
#   -r                        Restart the atrium systemd unit after installing (implies -i)

set -euo pipefail

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

while getopts ":ir" opt; do
    case $opt in
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
    meson setup build
fi
ninja -C build
EOF

if [[ $INSTALL -eq 1 ]]; then
    echo "==> Installing on $HOST"
    ssh "$HOST" bash <<EOF
set -euo pipefail
cd $DEST
sudo ninja -C build install
EOF
fi

if [[ $RESTART -eq 1 ]]; then
    echo "==> Restarting atrium on $HOST"
    ssh "$HOST" sudo systemctl restart atrium
fi

echo "==> Done"
