libandroid_runtime_lazy_include = include_directories('include')

libandroid_runtime_lazy = static_library(
  'android_runtime_lazy',
  ['android_runtime_lazy.cpp'],
  include_directories: [
    libandroid_runtime_lazy_include,
    frameworks_native_includes,
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libnativehelper_dep,
    libbase_dep,
    libcutils_dep,
    libutils_dep,
    liblog_dep,
  ],
)

libandroid_runtime_lazy_dep = declare_dependency(
  include_directories: libandroid_runtime_lazy_include,
  link_with: libandroid_runtime_lazy,
)
