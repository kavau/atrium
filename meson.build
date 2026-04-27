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

# Dependencies

# libpam = dependency('libpam')
cc = meson.get_compiler('c')
libpam = cc.find_library('pam')

# Executables

executable('atrium',
  sources: [
    'daemon/auth/auth.c',
    'daemon/core/main.c',
    'daemon/core/session.c',
    'daemon/core/vt.c',
  ],
  dependencies: [
    libpam,
  ])

executable('atrium-auth-test',
  sources: [
    'daemon/auth/auth.c',
    'tools/auth-test.c',
  ],
  dependencies: [
    libpam,
  ])

executable('atrium-start-session',
  sources: [
    'daemon/auth/auth.c',
    'daemon/core/session.c',
    'daemon/core/vt.c',
    'tools/start-session.c',
  ],
  dependencies: [
    libpam,
  ])