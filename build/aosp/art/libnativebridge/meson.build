libnativebridge_includes = include_directories('include')

libnativebridge = shared_library(
  'nativebridge',
  'native_bridge.cc',
  include_directories: libnativebridge_includes,
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [libnativehelper_dep, libbase_dep, liblog_dep],
  install: true,
)

libnativebridge_dep = declare_dependency(include_directories: libnativebridge_includes, link_with: libnativebridge)
