android_hardware_media_1_0 = custom_target(
  'android_hardware_media_1_0',
  build_by_default: true,
  input: [
    'types.hal',
  ],
  output: [
    'android/hardware/media/1.0/types.h',
    'android/hardware/media/1.0/types.cpp',
  ],
  command: [
    hidl_gen,
    '-R',
    '-L', 'c++',
    '-randroid.hardware:'
    + meson.project_source_root() / 'hardware/interfaces',
    '-randroid.hidl:' + system_libhidl_transport_root, 'android.hardware.media@1.0',
    '-o', meson.current_build_dir(),
  ],
)

libandroid_hardware_media_1_0 = static_library(
  'android_hardware_media_1_0',
  android_hardware_media_1_0,
  include_directories: [system_core_includes],
  dependencies: [
    libutils_dep,
    libcutils_dep,
    liblog_dep,
    libbase_dep,
    libhidl_dep,
    libfmq_dep,
    #libhardware_dep,
    libhwbinder_dep,
    libandroid_hardware_graphics_common_1_0_dep,
  ],
)

libandroid_hardware_media_1_0_dep = declare_dependency(
  include_directories: '.',
  link_with: libandroid_hardware_media_1_0,
)
