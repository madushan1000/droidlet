libshaders_include = include_directories('include')

libshaders = static_library(
  'shaders',
  'shaders.cpp',
  include_directories: [
    libshaders_include,
    frameworks_native_libs_math_includes,
    system_core_includes,
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    liblog_dep,
    libbase_dep,
    libbinder_dep,
    libbinder_ndk_dep,
    libnativewindow_dep,
    libui_dep,
    libtonemap_dep,
    libaidl_android_hardware_graphics_common_dep,
  ],
)

libshaders_dep = declare_dependency(
  include_directories: libshaders_include,
  link_with: libshaders,
)
