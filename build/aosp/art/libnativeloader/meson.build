libnativeloader_includes = include_directories('include')

libnativeloader = shared_library(
  'nativeloader',
  'native_loader.cpp',
  include_directories: libnativeloader_includes,
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [libnativehelper_dep, libbase_dep, libartbase_dep, libnativebridge_dep, droidlet_host_native_loader_dep],
  link_with: bt_dl_bio,
  install: true,
)

libnativeloader_dep = declare_dependency(include_directories: libnativeloader_includes, link_with: libnativeloader)

