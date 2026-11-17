libui_includes = include_directories('include')

com_android_graphics_libui_flags = custom_target(
  input: 'libui_flags.aconfig',
  command: [
    gen_aconfig_cpp_stubs,
    '@INPUT@',
    'com.android.graphics.libui.flags',
  ],
  capture: true,
  output: 'com_android_graphics_libui_flags.h',
)

libui = shared_library(
  'ui',
  [
    'DebugUtils.cpp',
    'DependencyMonitor.cpp',
    'DeviceProductInfo.cpp',
    'DynamicDisplayInfo.cpp',
    'Fence.cpp',
    'FenceTime.cpp',
    'FrameStats.cpp',
    'Gralloc.cpp',
    #'Gralloc5.cpp',
    'GraphicBuffer.cpp',
    'GraphicBufferAllocator.cpp',
    'GraphicBufferMapper.cpp',
    'PictureProfileHandle.cpp',
    'PixelFormat.cpp',
    'PublicFormat.cpp',
    'StaticAsserts.cpp',

    'ColorSpace.cpp',
    'Rect.cpp',
    'Region.cpp',
    'Transform.cpp',

    com_android_graphics_libui_flags,
  ],
  include_directories: [
    libui_includes,
    frameworks_native_includes,
    droidlet_includes,
    libsystem_includes,
    hardware_graphics_mapper_inclueds,
    'include_types',
    'include_private',
    '../math/include',
    '../arect/include',
    '../nativewindow/include',
    '../nativebase/include',
  ],
  cpp_args: ['-std=c++20', '-D__INTRODUCED_IN(x)=', '-DDROIDLET_HOST_GRALLOC'],
  dependencies: [
    droidlet_host_gralloc_dep,
    libutils_dep,
    libcutils_dep,
    liblog_dep,
    libbase_dep,
    libsync_dep,
    libaidl_android_hardware_common_dep,
    libaidl_android_hardware_graphics_common_dep,
    libimapper_dep,
    libbinder_ndk_dep,
    libaidlcommonsupport_dep,
  ],
  install: true,
)

libui_dep = declare_dependency(
  include_directories: [libui_includes, 'include_vndk', droidlet_includes],
  link_with: libui,
  dependencies: [
    libaidl_android_hardware_common_dep,
    libaidl_android_hardware_graphics_common_dep,
    libimapper_dep,
    libaidlcommonsupport_dep,
  ],
)
