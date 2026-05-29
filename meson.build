project('atrium', 'c',
  version: '0.2.0',
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

# Greeter install path: most distros follow the FHS convention /usr/libexec/,
# but Arch keeps internal helpers under lib/<pkg>/.
if dist == 'arch'
  greeter_install_dir = get_option('prefix') / 'lib' / 'atrium'
else
  greeter_install_dir = get_option('prefix') / get_option('libexecdir')
endif
add_project_arguments('-DATRIUM_GREETER_DIR="' + greeter_install_dir + '"', language: 'c')

# Dependencies

cc = meson.get_compiler('c')
dep_libpam     = cc.find_library('pam')  # libpam does not ship a .pc file on some distros
dep_libsystemd = dependency('libsystemd')
dep_gtk4       = dependency('gtk4')

# Feature detection

have_pam_start_confdir = cc.has_function(
    'pam_start_confdir',
    dependencies: dep_libpam,
)
if have_pam_start_confdir
    add_project_arguments('-DHAVE_PAM_START_CONFDIR=1', language: 'c')
endif

# Greeter CSS (compiled-in)

gnome = import('gnome')
greeter_resources = gnome.compile_resources(
  'greeter-resources',
  'greeter/greeter.gresource.xml',
  source_dir: 'greeter',
)

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
  install_tag: 'daemon',
)

executable('atrium-txt-greeter',
  sources: [
    'greeter/main-txt.c',
    'lib/ipc.c',
  ],
  dependencies: [
  ],
  install: true,
  install_dir: greeter_install_dir,
  install_tag: 'greeter',
)

executable('atrium-gtk-greeter',
  sources: [
    greeter_resources,
    'greeter/ipc.c',
    'greeter/main-gtk.c',
    'greeter/theme.c',
    'greeter/ui-gtk.c',
    'greeter/users.c',
    'lib/ipc.c',
  ],
  dependencies: [
    dep_gtk4,
  ],
  install: true,
  install_dir: greeter_install_dir,
  install_tag: 'greeter',
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
  install: false,
)

executable('atrium-fake-greeter',
  sources: [
    'lib/ipc.c',
    'tools/fake-greeter.c',
  ],
  dependencies: [
  ],
  install: false,
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

executable('atrium-greeter-test',
  sources: [
    'lib/ipc.c',
    'lib/proc.c',
    'tools/greeter-test.c',
  ],
  dependencies: [
  ],
  install: false,
)

# Data

install_data(
  'data/atrium.service',
  install_dir: '/usr/lib/systemd/system',
  install_tag: 'daemon',
)

install_data(
  'data/pam.d/atrium.' + dist,
  rename: 'atrium',
  install_dir: '/etc/pam.d',
  install_tag: 'daemon',
)

# Create the atriumdm system user and home directory. These hooks are run by
# the package manager on supported distros.
install_data(
  'data/sysusers.d/atrium.conf',
  install_dir: '/usr/lib/sysusers.d',
  install_tag: 'daemon',
)
install_data(
  'data/tmpfiles.d/atrium.conf',
  install_dir: '/usr/lib/tmpfiles.d',
  install_tag: 'daemon',
)

# For source installs, create the atriumdm user and home directory directly.
meson.add_install_script('systemd-sysusers', '/usr/lib/sysusers.d/atrium.conf',
  skip_if_destdir: true)
meson.add_install_script('systemd-tmpfiles', '--create', '/usr/lib/tmpfiles.d/atrium.conf',
  skip_if_destdir: true)
