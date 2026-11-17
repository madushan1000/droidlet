liblog = shared_library(
  'log',
  [
    'log_event_list.cpp',
    'log_event_write.cpp',
    'logger_name.cpp',
    'logger_read.cpp',
    'logger_write.cpp',
    'logprint.cpp',
    'properties.cpp',
  ],
  include_directories: ['include', libcutils_includes, libbase_includes],
  #dependencies: [libc_bio_dep],
  #cpp_args: ['-std=c++23'],
  install: true,
)

liblog_dep = declare_dependency(
  link_with: [liblog],
  include_directories: ['include'],
)
