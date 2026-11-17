libopenjdkjvm = shared_library(
  'openjdkjvm',
  'OpenjdkJvm.cc',
  include_directories: libart_runtime_includes,
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [
    libartbase_dep,
    libbase_dep,
    libnativehelper_dep,
    libdexfile_dep,
    libart_dep,
    com_android_art_flags_cpp_dep,
  ],
  install: true,
)

libopenjdkjvm_dep = declare_dependency(link_with: libopenjdkjvm)
