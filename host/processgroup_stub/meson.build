# Host replacement for libprocessgroup.  The host has no cgroups, so task
# profiles, cpusets and process groups are no-ops.  This lets libandroid_runtime
# keep the android.os.Process JNI without building the real library and its
# jsoncpp dependency.

libprocessgroup_stub_includes = include_directories('include')

libprocessgroup_stub = static_library(
  'processgroup_stub',
  'processgroup_stub.cpp',
  include_directories: libprocessgroup_stub_includes,
  cpp_args: ['-std=c++20'],
)

libprocessgroup_dep = declare_dependency(
  include_directories: libprocessgroup_stub_includes,
  link_with: libprocessgroup_stub,
)
