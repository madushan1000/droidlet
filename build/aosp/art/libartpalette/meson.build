libartpalette = shared_library(
  'artpalette',
  [
    'system/palette_fake.cc',
  ],
  include_directories: ['include'],
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [libnativehelper_dep, liblog_dep, libbase_dep],
  install: true,
)

libartpalette_dep = declare_dependency(link_with: libartpalette, include_directories: ['include'])

