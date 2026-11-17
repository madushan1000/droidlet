# Host gralloc allocator/mapper.
#
# Built after hardware/interfaces/{common,graphics/common}/aidl so the
# generated AIDL headers exist before this target is compiled.

# --- host gralloc allocator/mapper ---------------------------------------
droidlet_host_gralloc_includes = include_directories('include')
droidlet_host_gralloc = static_library(
  'droidlet_host_gralloc',
  'GrallocHost.cpp',
  include_directories: [
    droidlet_host_gralloc_includes,
    include_directories('../../../frameworks/native/libs/ui/include'),
    include_directories('../../../frameworks/native/libs/nativebase/include'),
    include_directories('../../../frameworks/native/libs/nativewindow/include'),
    include_directories('../../../frameworks/native/libs/arect/include'),
    frameworks_native_libs_math_includes,
    include_directories('../../../hardware/interfaces/graphics/mapper/stable-c/include'),
    system_core_includes,
    libcutils_includes,
    libbase_includes,
    include_directories('../../../system/logging/liblog/include'),
    include_directories('../../../system/core/libsync/include'),
    include_directories('../../../frameworks/native/libs/binder/ndk/include_cpp'),
    include_directories('../../../frameworks/native/libs/binder/ndk/include_ndk'),
    droidlet_includes,
  ],
  cpp_args: ['-std=c++20', '-D__INTRODUCED_IN(x)=', '-DDROIDLET_HOST=1'],
  dependencies: [
    libaidl_android_hardware_graphics_common_dep,
    libaidl_android_hardware_common_dep,
  ],
)
droidlet_host_gralloc_dep = declare_dependency(
  include_directories: droidlet_host_gralloc_includes,
  link_with: droidlet_host_gralloc,
)

