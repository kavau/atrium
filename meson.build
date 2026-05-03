project('atrium', 'c',
  version: '0.0.0',
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

if get_option('debug_logging')
  add_project_arguments('-DATRIUM_DEBUG', language: 'c')
endif

# Dependencies

# dep_libpam = dependency('libpam')
cc = meson.get_compiler('c')
dep_libpam = cc.find_library('pam')

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
    'daemon/core/main.c',
    'daemon/core/seat.c',
    'daemon/core/session.c',
    'daemon/core/vt.c',
    'daemon/session/auth.c',
    'daemon/session/session_runner.c',
  ],
  dependencies: [
    dep_libpam,
  ])

executable('atrium-auth-test',
  sources: [
    'daemon/session/auth.c',
    'tools/auth-test.c',
  ],
  dependencies: [
    dep_libpam,
  ])

executable('atrium-start-session',
  sources: [
    'daemon/core/vt.c',
    'daemon/core/session.c',
    'daemon/session/auth.c',
    'daemon/session/session_runner.c',
    'tools/start-session.c',
  ],
  dependencies: [
    dep_libpam,
  ])
