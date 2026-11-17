libaidlcommonsupport_includes = include_directories('include')

libaidlcommonsupport = static_library(
  'aidlcommonsupport',
  'NativeHandle.cpp',
  include_directories: libaidlcommonsupport_includes,
  dependencies: [
    libaidl_android_hardware_common_dep,
    libbinder_ndk_dep,
    libcutils_dep,
    libutils_dep,
  ],
)

libaidlcommonsupport_dep = declare_dependency(
  include_directories: libaidlcommonsupport_includes,
  link_with: libaidlcommonsupport,
)
