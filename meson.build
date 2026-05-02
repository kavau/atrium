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

if get_option('debug_logging')
  add_project_arguments('-DATRIUM_DEBUG', language: 'c')
endif

# Dependencies

# libpam = dependency('libpam')
cc = meson.get_compiler('c')
libpam = cc.find_library('pam')

# Executables

executable('atrium',
  sources: [
    'daemon/core/main.c',
    'daemon/core/session.c',
    'daemon/core/vt.c',
    'daemon/session/auth.c',
    'daemon/session/session_runner.c',
  ],
  dependencies: [
    libpam,
  ])

executable('atrium-auth-test',
  sources: [
    'daemon/session/auth.c',
    'tools/auth-test.c',
  ],
  dependencies: [
    libpam,
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
    libpam,
  ])