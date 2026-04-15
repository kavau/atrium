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

# Dependencies — declared here, consumed by subdirectories.
dep_libsystemd = dependency('libsystemd')
dep_libpam     = dependency('pam')
dep_gtk4       = dependency('gtk4')

subdir('src')
