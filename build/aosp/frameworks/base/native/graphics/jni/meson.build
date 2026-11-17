# Host libjnigraphics for NDK apps that use AndroidBitmap_*.
libjnigraphics = shared_library(
  'jnigraphics',
  'bitmap.cpp',
  include_directories: [
    libhwui_includes,
    system_core_includes,
    frameworks_native_includes,
    frameworks_native_private_includes,
    droidlet_includes,
  ],
  cpp_args: [
    '-std=c++20',
    '-D__INTRODUCED_IN(x)=',
    '-D__ANDROID_API__=26',
  ],
  dependencies: [
    libbase_dep,
    liblog_dep,
    libnativehelper_dep,
    libnativewindow_dep,
    libui_dep,
    libhwui_dep,
  ],
  install: true,
)
