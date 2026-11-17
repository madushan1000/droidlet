libsync_inlcudes = include_directories('include')

libsync = shared_library(
  'sync',
  'sync.c',
  c_args: ['-D__INTRODUCED_IN(x)='],
  include_directories: libsync_inlcudes,
  install: true,
)

libsync_dep = declare_dependency(
  include_directories: libsync_inlcudes,
  link_with: libsync,
)
