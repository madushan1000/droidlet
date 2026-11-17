operator_gen_in = [
  'arch/instruction_set.h',
  'base/allocator.h',
  'base/unix_file/fd_file.h',
]
operator_gen_outs = []
foreach name : operator_gen_in
  operator_gen_outs += operator_srcs_gen.process(name, extra_args: 'art/libartbase')
endforeach

libartbase_includes = include_directories(['.', 'base'])

libartbase = shared_library(
  'artbase',
  operator_gen_outs
  + [
    'arch/instruction_set.cc',
    'base/allocator.cc',
    'base/arena_allocator.cc',
    'base/arena_bit_vector.cc',
    'base/bit_vector.cc',
    'base/compiler_filter.cc',
    'base/file_magic.cc',
    'base/file_utils.cc',
    'base/flags.cc',
    'base/hex_dump.cc',
    'base/logging.cc',
    'base/calloc_arena_pool.cc',
    'base/membarrier.cc',
    'base/memfd.cc',
    'base/memory_region.cc',
    'base/mem_map.cc',
    'base/metrics/metrics_common.cc',
    'base/os_linux.cc',
    'base/pointer_size.cc',
    'base/runtime_debug.cc',
    'base/scoped_arena_allocator.cc',
    'base/scoped_flock.cc',
    'base/socket_peer_is_trusted.cc',
    'base/time_utils.cc',
    'base/unix_file/fd_file.cc',
    'base/unix_file/random_access_file_utils.cc',
    'base/utils.cc',
    'base/zip_archive.cc',

    'base/globals_unix.cc',
    'base/mem_map_unix.cc',
    com_android_art_rw_flags_cpp,
  ],
  include_directories: libartbase_includes,
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [
    libbase_dep,
    libnativehelper_dep,
    tinyxml2_dep,
    libziparchive_dep,
    libfmt_dep,
    com_android_art_rw_flags_cpp_dep,
  ],
  install: true,
)

libartbase_dep = declare_dependency(link_with: libartbase, include_directories: libartbase_includes)
