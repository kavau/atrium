executable('atrium',
  sources: [
    'main.c',
    'event.c',
    'seat.c',
    'bus.c',
    'vt.c',
    'session.c',
    'greeter.c',
    'auth.c',
    'config_file.c',
  ],
  dependencies: [
    dep_libsystemd,
    dep_libpam,
  ],
  include_directories: inc_lib,
  install: true,
)
