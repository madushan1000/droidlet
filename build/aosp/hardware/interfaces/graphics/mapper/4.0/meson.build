android_hardware_graphics_mapper_4_0 = custom_target(
  'android_hardware_graphics_mapper_4_0',
  build_by_default: true,
  input: 'IMapper.hal',
  output: [
    'android/hardware/graphics/mapper/4.0/IMapper.h',
    'android/hardware/graphics/mapper/4.0/MapperAll.cpp',
  ],
  command: [
    hidl_gen,
    '-R',
    '-L', 'c++',
    '-randroid.hardware:'
    + meson.project_source_root() / 'hardware/interfaces',
    '-randroid.hidl:' + system_libhidl_transport_root, 'android.hardware.graphics.mapper@4.0',
    '-o', meson.current_build_dir(),
  ],
)

libandroid_hardware_graphics_mapper_4_0 = static_library(
  'android_hardware_graphics_mapper_4_0',
  android_hardware_graphics_mapper_4_0,
  include_directories: [system_core_includes],
  dependencies: [
    libutils_dep,
    liblog_dep,
    libcutils_dep,
    libhidl_dep,
    libfmq_dep,
    libhwbinder_dep,
    libbase_dep,
    libandroid_hardware_graphics_common_1_0_dep,
    libandroid_hardware_graphics_common_1_1_dep,
    libandroid_hardware_graphics_common_1_2_dep,
  ],
)

libandroid_hardware_graphics_mapper_4_0_dep = declare_dependency(
  include_directories: '.',
  link_with: libandroid_hardware_graphics_mapper_4_0,
)
