# Host replacement for the few pieces of libincfs used by libandroidfw and
# libandroid_runtime.  The real IncFS driver is only relevant to incremental
# (streaming) installs; droidlet uses local APK files.

libincfs_stub_includes = include_directories(
  'include',
)

libincfs_stub = static_library(
  'incfs_stub',
  [
    'map_ptr.cpp',
    'incfs_ndk_stub.cpp',
  ],
  include_directories: [
    libincfs_stub_includes,
    bionic_kernel_includes,
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libbase_dep,
    libutils_dep,
  ],
)

libincfs_stub_dep = declare_dependency(
  include_directories: libincfs_stub_includes,
  link_with: libincfs_stub,
)
