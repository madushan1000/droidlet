libbase_includes = include_directories('include')
libbase = shared_library(
  'base',
  [
    'chrono_utils.cpp',
    'cmsg.cpp',
    'file.cpp',
    'hex.cpp',
    'logging.cpp',
    'mapped_file.cpp',
    'parsebool.cpp',
    'parsenetaddress.cpp',
    'posix_strerror_r.cpp',
    'process.cpp',
    'properties.cpp',
    'result.cpp',
    'stringprintf.cpp',
    'strings.cpp',
    'threads.cpp',
    'test_utils.cpp',
    'errors_unix.cpp',
  ],
  include_directories: [libbase_includes] ,
  cpp_args: ['-std=gnu++23'],
  dependencies: [liblog_dep],
  install: true,
)
libbase_dep = declare_dependency(include_directories: libbase_includes, link_with: libbase)
