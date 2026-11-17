libziparchive = shared_library(
  'ziparchive',
  [
    'zip_archive.cc',
    'zip_archive_stream_entry.cc',
    'zip_cd_entry_map.cc',
    'zip_error.cpp',
    'zip_writer.cc',
  ],
  include_directories: ['incfs_support/include', 'include'],
  cpp_args: [
    '-std=c++20',
    '-DZLIB_CONST',
    '-D_FILE_OFFSET_BITS=64',
  ],
  dependencies: [libbase_dep, liblog_dep, libz_dep],
  install: true,
)

libziparchive_dep = declare_dependency(link_with: [libziparchive,], include_directories: 'include')
