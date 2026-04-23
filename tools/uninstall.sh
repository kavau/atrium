#!/bin/sh
# Uninstall atrium: stop and disable the service, remove installed files,
# and remove the greeter system user.
#
# Usage:  sudo ./tools/uninstall.sh [--force|-f] [build-dir]
# Default build-dir is "build".

set -e

FORCE=0
if [ "$1" = "--force" ] || [ "$1" = "-f" ]; then
    FORCE=1
    shift
fi

BUILD_DIR="${1:-build}"
GREETER_USER="atriumdm"

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: this script must be run as root (try: sudo $0)"
    exit 1
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: build directory '$BUILD_DIR' not found"
    echo "Pass the build directory as an argument: sudo $0 <build-dir>"
    exit 1
fi

# ── Safety checks ────────────────────────────────────────────────────────────
# Warn about two conditions that would leave a non-savvy user with a broken
# or interrupted system.  Skip with --force.

ATRIUM_ACTIVE=0
if systemctl is-active --quiet atrium 2>/dev/null; then
    ATRIUM_ACTIVE=1
fi

NEEDS_ALTERNATIVE_DM=0
DM_LINK=$(readlink -f /etc/systemd/system/display-manager.service 2>/dev/null || true)
DM_NAME=""
if [ -n "$DM_LINK" ] && [ -e "$DM_LINK" ]; then
    DM_NAME=$(basename "$DM_LINK" .service)
fi
if [ -z "$DM_NAME" ] || [ "$DM_NAME" = "atrium" ]; then
    NEEDS_ALTERNATIVE_DM=1
fi

if [ "$FORCE" -ne 1 ] && { [ "$ATRIUM_ACTIVE" -eq 1 ] || [ "$NEEDS_ALTERNATIVE_DM" -eq 1 ]; }; then
    echo "WARNING:"
    if [ "$ATRIUM_ACTIVE" -eq 1 ]; then
        echo "  - atrium is currently running. Stopping it will end any graphical"
        echo "    session in progress. Run this script from a TTY (Ctrl+Alt+F2)"
        echo "    to avoid losing your current session."
    fi
    if [ "$NEEDS_ALTERNATIVE_DM" -eq 1 ]; then
        echo "  - no alternative display manager is enabled. After uninstalling,"
        echo "    the next boot will have no graphical login. Enable another DM"
        echo "    first, e.g.: 'sudo systemctl enable gdm' (or sddm, lightdm)."
    fi
    echo
    printf "Continue anyway? [y/N] "
    read -r answer
    case "$answer" in
        [yY]|[yY][eE][sS]) ;;
        *) echo "Aborted."; exit 0 ;;
    esac
fi

echo "Stopping atrium service (if running)..."
systemctl stop atrium 2>/dev/null || true

echo "Disabling atrium service (if enabled)..."
systemctl disable atrium 2>/dev/null || true

echo "Removing installed files via 'ninja uninstall'..."
ninja -C "$BUILD_DIR" uninstall

echo "Removing system user '$GREETER_USER' (if present)..."
if id "$GREETER_USER" >/dev/null 2>&1; then
    if userdel "$GREETER_USER" 2>/dev/null; then
        echo "User '$GREETER_USER' removed."
    else
        echo "WARNING: could not remove user '$GREETER_USER' (active processes or owned files?)"
        echo "Remove manually after cleanup: sudo userdel $GREETER_USER"
    fi
else
    echo "User '$GREETER_USER' does not exist, skipping."
fi

echo "Reloading systemd..."
systemctl daemon-reload

echo "atrium uninstalled."
