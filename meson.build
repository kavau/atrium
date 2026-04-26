project('atrium', 'c',
  version: '0.2.0',
  license: 'GPL-2.0-or-later',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'sysconfdir=/etc',
  ],
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

# Compile-time path to the installed atrium-greeter binary.  Used as the
# default greeter command in config_file.c when /etc/atrium.conf is absent.
add_project_arguments(
  '-DATRIUM_GREETER_PATH="' + (get_option('prefix') / get_option('libexecdir') / 'atrium-greeter') + '"',
  language: 'c',
)

# Compile-time sysconfdir so the daemon and the installed config file always
# agree on the path.  Defaults to /etc (set in default_options above).
add_project_arguments(
  '-DATRIUM_SYSCONFDIR="' + get_option('sysconfdir') + '"',
  language: 'c',
)

# Enable debug logging for debug and debugoptimized builds.
if get_option('buildtype').startswith('debug')
  add_project_arguments('-DATRIUM_DEBUG', language: 'c')
endif

# Shared include directory for lib/log.h, lib/conf_parse.h etc.
inc_lib = include_directories('lib')

# Shared config-file parser used by both the daemon and the greeter.
lib_conf_parse = static_library('conf_parse',
  'lib/conf_parse.c',
  include_directories: inc_lib,
)
dep_conf_parse = declare_dependency(
  link_with: lib_conf_parse,
  include_directories: inc_lib,
)

# Dependencies — declared here, consumed by subdirectories.
dep_libsystemd = dependency('libsystemd')
# libpam does not ship a .pc file on Debian; dependency('pam') falls back to
# a direct linker probe (-lpam) which is sufficient for our purposes.
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

# src/ is added to the greeter's include path so ui-gtk4.c can read
# CONFIG_MAX_*_LEN and CONFIG_GREETER_USER from config.h.
inc_src = include_directories('src')

executable('atrium-greeter',
  sources: [
    'greeter/main.c',
    'greeter/greeter_config.c',
    'greeter/users.c',
    'greeter/ui-gtk4.c',
  ],
  dependencies: [
    dep_gtk4,
    dep_conf_parse,
  ],
  include_directories: [inc_lib, inc_src],
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

# ── Configuration file ────────────────────────────────────────────────────────
#
# Generate /etc/atrium.conf from the template, substituting the installed path
# of atrium-greeter so the default greeter command is correct out of the box.

conf_data = configuration_data()
conf_data.set('greeter_bin',
  get_option('prefix') / get_option('libexecdir') / 'atrium-greeter')

configure_file(
  input:         'data/atrium.conf.in',
  output:        'atrium.conf',
  configuration: conf_data,
  install:       true,
  install_dir:   get_option('sysconfdir'),
)

install_data('data/atrium-greeter.conf',
  install_dir: get_option('sysconfdir'),
)

# ── PAM configuration ─────────────────────────────────────────────────────────
#
# Install the distro-appropriate PAM stack.  Controlled by the -Dpam_config
# meson option (default: arch).  See data/pam.d/ for the variants.

pam_variant = get_option('pam_config')
install_data('data/pam.d/atrium.' + pam_variant,
  install_dir: '/etc/pam.d',
  rename: 'atrium',
)

# ── Config file test tool ─────────────────────────────────────────────────────
#
# Loads /etc/atrium.conf and /etc/atrium-greeter.conf (or paths passed as
# arguments) and prints every resolved value.  Exercises conf_parse,
# config_file.c, and greeter_config.c with no daemon or session involvement.
#
#   ./build/atrium-config-test
#   ./build/atrium-config-test /tmp/test.conf /tmp/test-greeter.conf
#
# Not installed; run directly from build/.

executable('atrium-config-test',
  sources: [
    'tools/config-test.c',
    'src/config_file.c',
    'greeter/greeter_config.c',
  ],
  dependencies: [dep_conf_parse],
  include_directories: [include_directories('src'), include_directories('greeter'), inc_lib],
  c_args: ['-DLOG_PREFIX="config-test"'],
  install: false,
)

# ── PAM auth test tool ────────────────────────────────────────────────────────
#
# Validates src/auth.c against the real PAM stack with real credentials, in
# complete isolation from the daemon.  Run as root:
#
#   sudo ./build/atrium-auth-test <username>
#
# Not installed; run directly from build/.

executable('atrium-auth-test',
  sources: [
    'tools/auth-test.c',
    'src/auth.c',
  ],
  dependencies: [dep_libpam],
  include_directories: [include_directories('src'), inc_lib],
  c_args: ['-DLOG_PREFIX="auth-test"'],
  install: false,
)

# ── Install hook: create greeter user ─────────────────────────────────────────
#
# Automatically create the atriumdm system user during 'ninja install' if it
# doesn't already exist.  This removes the manual useradd step from the install
# instructions.

meson.add_install_script('tools/create-greeter-user.sh')
