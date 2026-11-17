libmeminfo_includes = include_directories(
  'include',
  'libdmabufinfo/include',
)

libmeminfo = shared_library(
  'meminfo',
  [
    'androidprocheaps.cpp',
    'pageacct.cpp',
    'procmeminfo.cpp',
    'sysmeminfo.cpp',

    'libdmabufinfo/dmabufinfo.cpp',
    'libdmabufinfo/dmabuf_sysfs_stats.cpp',
    'libdmabufinfo/dmabuf_bpf_stats.cpp',
    'libdmabufinfo/dmabuf_per_buffer_stats.cpp',
  ],
  include_directories: [libmeminfo_includes],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libbase_dep,
    libprocinfo_dep,
  ],
  install: true,
)
libmeminfo_dep = declare_dependency(
  include_directories: libmeminfo_includes,
  link_with: libmeminfo,
)
