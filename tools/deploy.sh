#!/bin/bash
# deploy.sh - sync source to a remote machine and build atrium there.

set -euo pipefail

USER="me"
HOST="my-remote-machine"
DEST="/tmp/atrium"

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
echo "==> Deploying from $REPO_DIR to $HOST:$DEST"

echo "==> Syncing tracked files to $HOST:$DEST"
cd "$REPO_DIR"
git ls-files | rsync -avz --delete --files-from=- . "$HOST:$DEST"

echo "==> Building on $HOST"
ssh "$USER@$HOST" bash -l <<EOF
set -euo pipefail
cd $DEST
if [[ ! -d build ]]; then
    echo "--- Running meson setup"
    meson setup build
else
    echo "--- Reconfiguring meson"
    meson setup build --reconfigure
fi
echo "--- Running ninja"
ninja -C build
EOF
