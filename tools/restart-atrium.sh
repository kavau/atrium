#!/bin/bash
# Restart atrium cleanly: terminate all graphical sessions, then restart the daemon.
# Run as: sudo tools/restart-atrium.sh

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: must be run as root (use sudo)" >&2
    exit 1
fi

trap '' SIGTERM SIGHUP

loginctl list-sessions --no-legend | awk '{print $1}' | while read -r id; do
    type=$(loginctl show-session "$id" -p Type --value 2>/dev/null || true)
    if [ "$type" = "wayland" ] || [ "$type" = "x11" ]; then
        echo "Terminating session $id (type=$type)" >&2
        loginctl terminate-session "$id"
    fi
done

systemctl restart atrium
