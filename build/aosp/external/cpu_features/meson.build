libcpu_features_includes = include_directories('include')

libcpu_features = static_library(
  'cpu_features',
  [
    'src/filesystem.c',
    'src/stack_line_reader.c',
    'src/string_view.c',
    'src/impl_x86_linux_or_android.c',
  ],
  c_args: [
    '-DSTACK_LINE_READER_BUFFER_SIZE=1024',
  ],
  include_directories: libcpu_features_includes,
)

libcpu_features_dep = declare_dependency(include_directories: libcpu_features_includes, link_with: libcpu_features)
