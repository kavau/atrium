#!/bin/sh
# Label the atrium binary xdm_exec_t, label /var/lib/atrium xdm_var_lib_t, and
# install the atrium-local SELinux policy module. Called during installation on
# Fedora builds.
# Skipped when DESTDIR is set (package builds handle labelling via triggers).

set -e

if [ -n "$DESTDIR" ]; then
    exit 0
fi

daemon_bin="$1"

if ! command -v semanage > /dev/null 2>&1; then
    echo "atrium: semanage not found -- install policycoreutils-python-utils" >&2
    echo "atrium: then re-run: sudo ninja -C <builddir> install" >&2
    echo "atrium: without this, SELinux will block session launch on enforcing systems" >&2
    exit 0
fi

# -a adds a new rule; -m modifies an existing one. We try -a first, and fall
# back to -m so that reinstalls do not fail on a rule that already exists.

# Label the daemon binary xdm_exec_t so systemd starts it in the xdm_t domain.
semanage fcontext -a -t xdm_exec_t "$daemon_bin" 2>/dev/null \
    || semanage fcontext -m -t xdm_exec_t "$daemon_bin"
restorecon -v "$daemon_bin"

# Label /var/lib/atrium xdm_var_lib_t (like GDM's /var/lib/gdm) so xdm_t can
# manage seat-state files there.
semanage fcontext -a -t xdm_var_lib_t "/var/lib/atrium(/.*)?" 2>/dev/null \
    || semanage fcontext -m -t xdm_var_lib_t "/var/lib/atrium(/.*)?"
if [ -d /var/lib/atrium ]; then
    restorecon -Rv /var/lib/atrium
fi

# Compile and install the atrium-local policy module, which grants xdm_t
# permission to run udevadm settle without a domain transition.
if ! command -v checkmodule > /dev/null 2>&1; then
    echo "atrium: checkmodule not found -- install checkpolicy" >&2
    echo "atrium: without it, 'udevadm settle' will be blocked" >&2
    exit 0
fi

TE_FILE="$MESON_SOURCE_ROOT/data/selinux/atrium-local.te"
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

checkmodule -M -m -o "$tmpdir/atrium-local.mod" "$TE_FILE"
semodule_package -o "$tmpdir/atrium-local.pp" -m "$tmpdir/atrium-local.mod"
semodule -i "$tmpdir/atrium-local.pp"
echo "atrium: SELinux module atrium-local installed"
