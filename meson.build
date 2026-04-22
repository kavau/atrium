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
libsystemd = dependency('libsystemd')

# Executables
executable('atrium',
  sources: [
    'daemon/core/main.c',
    'daemon/core/session.c',
  ],
  dependencies: [
    libsystemd,
  ])