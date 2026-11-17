android_hardware_graphics_mapper_2_1 = custom_target(
  'android_hardware_graphics_mapper_2_1',
  build_by_default: true,
  input: 'IMapper.hal',
  output: [
    'android/hardware/graphics/mapper/2.1/IMapper.h',
    'android/hardware/graphics/mapper/2.1/MapperAll.cpp',
  ],
  command: [
    hidl_gen,
    '-R',
    '-L', 'c++',
    '-randroid.hardware:' + meson.project_source_root() / 'hardware/interfaces', 
    '-randroid.hidl:' + system_libhidl_transport_root, 'android.hardware.graphics.mapper@2.1',
    '-o', meson.current_build_dir(),
  ],
)

libandroid_hardware_graphics_mapper_2_1 = static_library(
  'android_hardware_graphics_mapper_2_1',
  android_hardware_graphics_mapper_2_1,
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
    libandroid_hardware_graphics_mapper_2_0_dep,
  ]
)

libandroid_hardware_graphics_mapper_2_1_dep = declare_dependency(
  include_directories: '.',
  link_with: libandroid_hardware_graphics_mapper_2_1,
)
