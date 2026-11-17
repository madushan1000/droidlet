libcutils_includes = include_directories('include')

libcutils = shared_library(
  'cutils',
  [

    'config_utils.cpp',
    'iosched_policy.cpp',
    'load_file.cpp',
    'native_handle.cpp',
    'properties.cpp',
    'record_stream.cpp',
    'strlcpy.c',

    'canned_fs_config.cpp',
    'fs_config.cpp',

    'fs.cpp',
    'hashmap.cpp',
    'multiuser.cpp',
    'str_parms.cpp',

    'trace-host.cpp',
    'ashmem-host.cpp',
  ],
  include_directories: [libcutils_includes, system_core_includes],
  dependencies: [libbase_dep, liblog_dep],
  install: true,
)

libcutils_dep = declare_dependency(include_directories: libcutils_includes, link_with: libcutils)
