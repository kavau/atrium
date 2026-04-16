project('atrium', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

# Enable debug logging for debug and debugoptimized builds.
if get_option('buildtype').startswith('debug')
  add_project_arguments('-DATRIUM_DEBUG', language: 'c')
endif

# Shared include directory for lib/log.h etc.
inc_lib = include_directories('lib')

# Dependencies — declared here, consumed by subdirectories.
dep_libsystemd = dependency('libsystemd')
dep_libpam     = dependency('pam')
dep_gtk4       = dependency('gtk4')

subdir('src')

# ── Greeter binary ────────────────────────────────────────────────────────────
#
# atrium-greeter is the per-seat UI process spawned by the daemon inside a cage
# compositor session.  It lives in /usr/libexec because it is a helper that is
# only ever started by the daemon, never directly by a user or an admin.
#
# The UI implementation is in greeter/ui-gtk4.c.  To swap toolkits, replace
# that file and update the sources list here — no other changes required.

executable('atrium-greeter',
  sources: [
    'greeter/main.c',
    'greeter/users.c',
    'greeter/ui-gtk4.c',
  ],
  dependencies: [
    dep_gtk4,
  ],
  include_directories: inc_lib,
  c_args: ['-DLOG_PREFIX="atrium-greeter"'],
  install: true,
  install_dir: get_option('libexecdir'),
)

# ── Greeter IPC test tool ─────────────────────────────────────────────────────
#
# Standalone test that takes the role of the daemon: forks the greeter with
# credential/result pipes, reads submitted credentials, replies ok/fail.
# Not installed; run directly from build/.

executable('atrium-greeter-test',
  sources: [
    'tools/greeter-test.c',
  ],
  install: false,
)

# Install the systemd service unit.
install_data('data/atrium.service',
  install_dir: '/usr/lib/systemd/system',
)
