android_hardware_graphics_common_1_2 = custom_target(
  'android_hardware_graphics_common_1_2',
  build_by_default: true,
  input: 'types.hal',
  output: [
    'android/hardware/graphics/common/1.2/types.h',
    'android/hardware/graphics/common/1.2/types.cpp',
  ],
  command: [
    hidl_gen,
    '-R',
    '-L', 'c++',
    '-randroid.hardware:' + meson.project_source_root() / 'hardware/interfaces',
    '-randroid.hidl:' + system_libhidl_transport_root, 'android.hardware.graphics.common@1.2',
    '-o', meson.current_build_dir(),
  ],
)

libandroid_hardware_graphics_common_1_2 = static_library(
  'android_hardware_graphics_common_1_2',
  android_hardware_graphics_common_1_2,
  include_directories: [system_core_includes],
  dependencies: [
    libutils_dep,
    libcutils_dep,
    liblog_dep,
    libbase_dep,
    libhidl_dep,
    libfmq_dep,
    libhwbinder_dep,
    libandroid_hardware_graphics_common_1_0_dep,
    libandroid_hardware_graphics_common_1_1_dep,
  ],
)

libandroid_hardware_graphics_common_1_2_dep = declare_dependency(
  include_directories: '.',
  link_with: libandroid_hardware_graphics_common_1_2,
)
