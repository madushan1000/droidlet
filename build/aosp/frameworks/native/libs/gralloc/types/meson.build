libgralloc_includes = include_directories('include')

libgralloc = static_library(
  'gralloc',
  'Gralloc4.cpp',
  include_directories: libgralloc_includes,
  dependencies: [
    libutils_dep,
    libcutils_dep,
    libhidl_dep,
    liblog_dep,
    libbinder_ndk_dep,
    libaidl_android_hardware_graphics_common_dep,
    libandroid_hardware_graphics_common_1_0_dep,
    libandroid_hardware_graphics_common_1_1_dep,
    libandroid_hardware_graphics_common_1_2_dep,
    libandroid_hardware_graphics_mapper_4_0_dep,
    libfmq_dep,
  ]
)

libgralloc_dep = declare_dependency(
  include_directories: libgralloc_includes,
  link_with: libgralloc,
)
