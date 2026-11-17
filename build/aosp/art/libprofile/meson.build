libprofile_includes = include_directories('.')

libprofile = shared_library(
  'profile',
  [
    'profile/profile_boot_info.cc',
    'profile/profile_compilation_info.cc',
  ],
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [
    libbase_dep,
    libartbase_dep,
    libdexfile_dep,
    libnativehelper_dep,
    libartpalette_dep,
    libz_dep,
  ],
  install: true,
)

libprofile_dep = declare_dependency(include_directories: libprofile_includes, link_with: libprofile)
