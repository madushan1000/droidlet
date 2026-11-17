libbinder_ndk_includes = include_directories('include_cpp', 'include_ndk', 'include_platform')

libbinder_ndk = shared_library(
  'binder_ndk',
  [
    'ibinder.cpp',
    'libbinder.cpp',
    'parcel.cpp',
    'stability.cpp',
    'status.cpp',

    'ibinder_jni.cpp',
    'parcel_jni.cpp',
    'persistable_bundle.cpp',
    'process.cpp',
    'service_manager.cpp',
    'binder_rpc.cpp',
  ],
  include_directories: [
    libbinder_ndk_includes,
    system_core_includes,
    frameworks_base_core_jni,
    #bionic_kernel_includes,
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libbinder_dep,
    libbase_dep,
    libutils_dep,
    libcutils_dep,
    liblog_dep,
    libnativehelper_dep,
    libandroid_runtime_lazy_dep,
  ],
  install: true,
)

libbinder_ndk_dep = declare_dependency(
  include_directories: libbinder_ndk_includes,
  link_with: libbinder_ndk,
)
