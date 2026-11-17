#aidl_android_hardware_graphics_common_path = meson.current_source_dir()
framework_native_aidl_binder_cpp = custom_target(
  'framework_native_aidl_binder',
  input: ['aidl/binder/android/os/PersistableBundle.aidl'],
  output: [
    'android/os/BnPersistableBundle.h',
    'android/os/BpPersistableBundle.h',
    'android/os/PersistableBundle.h',
    'android/os/PersistableBundle.cpp',
  ],
  build_by_default: true,
  command: [
    aidl,
    '--lang=cpp',
    '-I', fs.relative_to(meson.current_source_dir() / 'aidl/binder', meson.project_build_root()),
    #'-I', aidl_android_hardware_common_path,
    #'--stability=vintf',
    #'--structured',
    '-o', meson.current_build_dir(),
    '-h', meson.current_build_dir(),
    '@INPUT@',
  ],
)

libframework_native_aidl_binder = shared_library(
  'framework_native_aidl_binder',
  framework_native_aidl_binder_cpp,
  cpp_args: ['-std=c++20'],
  dependencies: [
    #libaidl_android_hardware_common_dep,
    libbinder_dep,
    libbase_dep,
    libutils_dep,
    liblog_dep,
  ],
)

libframework_native_aidl_binder_dep = declare_dependency(
  include_directories: ['.', '..'],
  link_with: libframework_native_aidl_binder,
)

framework_native_aidl_gui_cpp = custom_target(
  'framework_native_aidl_gui',
  input: [
    'aidl/gui/android/view/LayerMetadataKey.aidl',
    'aidl/gui/android/view/Surface.aidl',
  ],
  output: [
    'android/view/BnLayerMetadataKey.h',
    'android/view/BnSurface.h',
    'android/view/BpLayerMetadataKey.h',
    'android/view/BpSurface.h',
    'android/view/LayerMetadataKey.h',
    'android/view/Surface.h',
    'android/view/Surface.cpp',
    'android/view/LayerMetadataKey.cpp',
  ],
  build_by_default: true,
  command: [
    aidl,
    '--lang=cpp',
    '-I', fs.relative_to(meson.current_source_dir() / 'aidl/gui', meson.project_build_root()),
    #'-I', aidl_android_hardware_common_path,
    #'--stability=vintf',
    #'--structured',
    '-o', meson.current_build_dir(),
    '-h', meson.current_build_dir(),
    '@INPUT@',
  ],
)

libframework_native_aidl_gui = shared_library(
  'framework_native_aidl_gui',
  framework_native_aidl_gui_cpp,
  cpp_args: ['-std=c++20'],
  dependencies: [
    #libaidl_android_hardware_common_dep,
    libbinder_dep,
    libbase_dep,
    libutils_dep,
    liblog_dep,
  ],
)

framework_native_aidl_gui_dep = declare_dependency(
  include_directories: ['.', '..'],
  link_with: libframework_native_aidl_gui,
)

framework_native_aidl_display_cpp = custom_target(
  'framework_native_aidl_display',
  input: [
    'aidl/android/hardware/display/IDeviceProductInfoConstants.aidl',
  ],
  output: [
    'android/hardware/display/BnDeviceProductInfoConstants.h',
    'android/hardware/display/BpDeviceProductInfoConstants.h',
    'android/hardware/display/IDeviceProductInfoConstants.h',
    'android/hardware/display/IDeviceProductInfoConstants.cpp',
  ],
  build_by_default: true,
  command: [
    aidl,
    '--lang=cpp',
    '-I', fs.relative_to(meson.current_source_dir() / 'aidl', meson.project_build_root()),
    #'-I', aidl_android_hardware_common_path,
    #'--stability=vintf',
    #'--structured',
    '-o', meson.current_build_dir(),
    '-h', meson.current_build_dir(),
    '@INPUT@',
  ],
)

libframework_native_aidl_display = shared_library(
  'framework_native_aidl_display',
  framework_native_aidl_display_cpp,
  cpp_args: ['-std=c++20'],
  dependencies: [
    #libaidl_android_hardware_common_dep,
    libbinder_dep,
    libbase_dep,
    libutils_dep,
    libcutils_dep,
    liblog_dep,
  ],
)

libframework_native_aidl_display_dep = declare_dependency(
  include_directories: ['.', '..'],
  link_with: libframework_native_aidl_display,
)
