libelffile_includes = include_directories('.')

libelffile = static_library(
  'elffile',
  [
    'elf/xz_utils.cc',
    'stream/buffered_output_stream.cc',
    'stream/file_output_stream.cc',
    'stream/output_stream.cc',
    'stream/vector_output_stream.cc',
  ],
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [
    libbase_dep, 
    libartbase_dep,
    liblzma_dep
  ]
)

libelffile_dep = declare_dependency(include_directories: libelffile_includes, link_with: libelffile)
