#!/bin/sh
# Create the atrium system user if it doesn't already exist.
# Called automatically during 'ninja install'.
#
# To change the username, update both:
#   - GREETER_USER below
#   - CONFIG_GREETER_USER in src/config.h

set -e

# Skip during staged/package installs — the packager is responsible for
# arranging user creation via package metadata (RPM %pre, deb postinst, etc.).
if [ -n "$DESTDIR" ]; then
    echo "DESTDIR set; skipping greeter user creation."
    exit 0
fi

GREETER_USER="atriumdm"

if id "$GREETER_USER" >/dev/null 2>&1; then
    # User exists - verify it's a system user with appropriate properties
    USER_UID=$(id -u "$GREETER_USER")
    USER_SHELL=$(getent passwd "$GREETER_USER" | cut -d: -f7)
    USER_HOME=$(getent passwd "$GREETER_USER" | cut -d: -f6)

    # System users typically have UID < 1000 (varies by distro, but a safe check)
    if [ "$USER_UID" -ge 1000 ]; then
        echo "ERROR: User '$GREETER_USER' exists but is not a system user (UID=$USER_UID >= 1000)."
        echo "Please either:"
        echo "  1. Remove the existing user and re-run install, or"
        echo "  2. Choose a different username by updating both:"
        echo "     - GREETER_USER in tools/create-greeter-user.sh"
        echo "     - CONFIG_GREETER_USER in src/config.h"
        exit 1
    fi

    # Warn if shell is not nologin/false (but don't fail - might be intentional for debugging)
    case "$USER_SHELL" in
        /usr/sbin/nologin|/sbin/nologin|/bin/false|/usr/bin/false) ;;
        *)
            echo "WARNING: User '$GREETER_USER' has shell '$USER_SHELL' (expected nologin or false)"
            ;;
    esac

    echo "User '$GREETER_USER' already exists (UID=$USER_UID), skipping creation."
else
    echo "Creating system user '$GREETER_USER'..."
    useradd --system --no-create-home --shell /usr/sbin/nologin "$GREETER_USER"
    echo "User '$GREETER_USER' created successfully."
fi
