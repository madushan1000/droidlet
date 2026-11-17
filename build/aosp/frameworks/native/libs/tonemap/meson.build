libtonemap_includes = include_directories('include')

libtonemap = static_library(
  'tonemap',
  'tonemap.cpp',
  include_directories: [libtonemap_includes, '../math/include'],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libaidl_android_hardware_graphics_common_dep,
    libaidl_android_hardware_graphics_composer_dep,
    libbinder_dep,
    libbinder_ndk_dep,
    libnativewindow_dep,
  ],
)

libtonemap_dep = declare_dependency(
  include_directories: libtonemap_includes,
  link_with: libtonemap,
  dependencies: [
    libaidl_android_hardware_graphics_common_dep,
    libaidl_android_hardware_graphics_composer_dep,
  ],
)
