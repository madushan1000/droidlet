aidl_android_hardware_graphics_allocator = custom_target(
  'aidl_android_hardware_graphics_allocator',
  input: [
    'android/hardware/graphics/allocator/AllocationError.aidl',
    'android/hardware/graphics/allocator/AllocationResult.aidl',
    'android/hardware/graphics/allocator/BufferDescriptorInfo.aidl',
    'android/hardware/graphics/allocator/IAllocator.aidl',
  ],
  output: [
    'android/hardware/graphics/allocator/AllocationError.cpp',
    'android/hardware/graphics/allocator/AllocationResult.cpp',
    'android/hardware/graphics/allocator/BufferDescriptorInfo.cpp',
    'android/hardware/graphics/allocator/IAllocator.cpp',
    'aidl/android/hardware/graphics/allocator/AllocationError.h',
    'aidl/android/hardware/graphics/allocator/AllocationResult.h',
    'aidl/android/hardware/graphics/allocator/BufferDescriptorInfo.h',
    'aidl/android/hardware/graphics/allocator/IAllocator.h',
  ],
  build_by_default: true,
  command: [
    aidl,
    '--lang=ndk',
    '-I', fs.relative_to(meson.current_source_dir(), meson.project_build_root()),
    '-I', aidl_android_hardware_common_path,
    '-I', aidl_android_hardware_graphics_common_path,
    '--stability=vintf',
    '--structured',
    '-o', meson.current_build_dir(),
    '-h', meson.current_build_dir(),
    '--version=1',
    '@INPUT@',
  ],
)

libaidl_android_hardware_graphics_allocator = static_library(
  'aidl_android_hardware_graphics_allocator',
  aidl_android_hardware_graphics_allocator,
  cpp_args: ['-std=c++20'],
  dependencies: [
    libaidl_android_hardware_common_dep,
    libaidl_android_hardware_graphics_common_dep,
    libbinder_dep,
    libbase_dep,
    libutils_dep,
    liblog_dep,
  ],
)

libaidl_android_hardware_graphics_allocator_dep = declare_dependency(
  include_directories: ['.', '..'],
  link_with: libaidl_android_hardware_graphics_allocator,
)
