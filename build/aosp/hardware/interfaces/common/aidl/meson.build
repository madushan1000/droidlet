fs = import('fs')
aidl_android_hardware_common_path = meson.current_source_dir()
aidl_android_hardware_common = custom_target(
  'aidl_android_hardware_common',
  input: [
    'android/hardware/common/Ashmem.aidl',
    'android/hardware/common/NativeHandle.aidl',
    'android/hardware/common/MappableFile.aidl',
  ],
  output: [
    'android/hardware/common/Ashmem.cpp',
    'android/hardware/common/MappableFile.cpp',
    'android/hardware/common/NativeHandle.cpp',
    'aidl/android/hardware/common/Ashmem.h',
    'aidl/android/hardware/common/MappableFile.h',
    'aidl/android/hardware/common/NativeHandle.h',
  ],
  command: [
    aidl,
    '--lang=ndk',
    '-I', fs.relative_to(meson.current_source_dir(), meson.project_build_root()),
    '--stability=vintf',
    '--structured',
    '-o', meson.current_build_dir(),
    '-h', meson.current_build_dir(),
    '@INPUT@',
  ],
)

libaidl_android_hardware_common = static_library(
  'aidl_android_hardware_common',
  aidl_android_hardware_common,
  dependencies: [
    libbinder_ndk_dep,
    libbase_dep,
    libutils_dep,
    liblog_dep,
  ],
)

libaidl_android_hardware_common_dep = declare_dependency(
  include_directories: ['.', '..', '../support/include'],
  link_with: libaidl_android_hardware_common,
)
