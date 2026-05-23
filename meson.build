project('atrium', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

add_project_arguments(
  '-D_GNU_SOURCE',
  '-Wcast-align',
  '-Werror=return-type',
  '-Wformat=2',
  '-Wmissing-prototypes',
  '-Wnull-dereference',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wunused-parameter',
  language: 'c',
)

# Build options

if get_option('dist') == 'unset'
  error('-Ddist is required. Choose one of: arch, debian, fedora')
endif
dist = get_option('dist')

if get_option('debug_logging')
  add_project_arguments('-DATRIUM_DEBUG', language: 'c')
endif

# Greeter install path: most distros follow the FHS convention /usr/libexec/;
# Arch keeps internal helpers under lib/<pkg>/.
if dist == 'arch'
  greeter_install_dir = get_option('prefix') / 'lib' / 'atrium'
else
  greeter_install_dir = get_option('libexecdir')
endif
greeter_path = greeter_install_dir / 'atrium-greeter'
add_project_arguments('-DATRIUM_GREETER_PATH="' + greeter_path + '"', language: 'c')

# Dependencies

cc = meson.get_compiler('c')
dep_libpam     = cc.find_library('pam')  # libpam does not ship a .pc file on some distros
dep_libsystemd = dependency('libsystemd')

# Feature detection

have_pam_start_confdir = cc.has_function(
    'pam_start_confdir',
    dependencies: dep_libpam,
)
if have_pam_start_confdir
    add_project_arguments('-DHAVE_PAM_START_CONFDIR=1', language: 'c')
endif

# Executables

executable('atrium',
  sources: [
    'daemon/core/runner.c',
    'daemon/core/main.c',
    'daemon/core/seat.c',
    'daemon/core/vt.c',
    'daemon/session/auth.c',
    'daemon/session/bus.c',
    'daemon/session/session_compositor.c',
    'daemon/session/session_exec.c',
    'daemon/session/session_greeter.c',
    'daemon/session/session_runner.c',
    'lib/ipc.c',
    'lib/proc.c',
  ],
  dependencies: [
    dep_libpam,
    dep_libsystemd,
  ],
  install: true,
)

executable('atrium-greeter',
  sources: [
    'greeter/main.c',
    'lib/ipc.c',
  ],
  dependencies: [
  ],
  install: true,
  install_dir: greeter_install_dir,
)

# Tools (not installed)

executable('atrium-auth-test',
  sources: [
    'daemon/session/auth.c',
    'tools/auth-test.c',
  ],
  dependencies: [
    dep_libpam,
  ],
  install: false,
)

executable('atrium-start-session',
  sources: [
    'daemon/core/vt.c',
    'daemon/session/auth.c',
    'daemon/session/bus.c',
    'daemon/session/session_compositor.c',
    'daemon/session/session_exec.c',
    'daemon/session/session_greeter.c',
    'daemon/session/session_runner.c',
    'lib/ipc.c',
    'lib/proc.c',
    'tools/start-session.c',
  ],
  dependencies: [
    dep_libpam,
    dep_libsystemd,
  ],
  install: false,
)

executable('atrium-fake-compositor',
  sources: [
    'tools/fake-compositor.c',
  ],
  dependencies: [
  ],
  install: false
)

executable('atrium-fake-greeter',
  sources: [
    'lib/ipc.c',
    'tools/fake-greeter.c',
  ],
  dependencies: [
  ],
  install: false
)

executable('atrium-ipc-test',
  sources: [
    'lib/ipc.c',
    'tools/ipc-test.c',
  ],
  dependencies: [
  ],
  install: false,
)

# Data

install_data(
  'data/pam.d/atrium.' + dist,
  rename: 'atrium',
  install_dir: '/etc/pam.d',
)

# Create the atriumdm system user via systemd-sysusers. Package managers on
# supported distros fire the sysusers hook automatically on install.
install_data(
  'data/sysusers.d/atrium.conf',
  install_dir: '/usr/lib/sysusers.d',
)

# For source installs, call systemd-sysusers directly to create the atriumdm
# user (package manager installs skip this).
meson.add_install_script('systemd-sysusers', '/usr/lib/sysusers.d/atrium.conf',
  skip_if_destdir: true)

install_data(
  'data/atrium.service',
  install_dir: '/usr/lib/systemd/system',
)