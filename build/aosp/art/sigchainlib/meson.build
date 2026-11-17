libsigchain = shared_library(
  'sigchain',
  'sigchain.cc',
  cpp_args: art_default_cflags
  + [
    '-std=c++26',
    '-DSYS_SECCOMP=1',
  ],
  install: true,
)

libsigchain_dep = declare_dependency(
  include_directories: '.',
  link_with: libsigchain,
  compile_args: [

    '-DSYS_SECCOMP=1',
  ],
)
