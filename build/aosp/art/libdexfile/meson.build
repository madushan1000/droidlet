dexfile_operator_gen_in = [
  'dex/dex_file.h',
  'dex/dex_instruction.h',
  'dex/dex_instruction_utils.h',
  'dex/invoke_type.h',
]
dexfile_operator_gen_outs = []
foreach name : dexfile_operator_gen_in
  dexfile_operator_gen_outs += operator_srcs_gen.process(name, extra_args: 'art/libdexfile')
endforeach

libdexfile_includes = ['external/include', 'dex', '.']

libdexfile = shared_library(
  'dexfile',
  dexfile_operator_gen_outs
  + [
    'external/dex_file_ext.cc',
    'external/dex_file_supp.cc',
    'dex/art_dex_file_loader.cc',
    'dex/descriptors_names.cc',
    'dex/dex_file.cc',
    'dex/dex_file_exception_helpers.cc',
    'dex/dex_file_loader.cc',
    'dex/dex_file_tracking_registrar.cc',
    'dex/dex_file_verifier.cc',
    'dex/dex_instruction.cc',
    'dex/modifiers.cc',
    'dex/primitive.cc',
    'dex/signature.cc',
    'dex/standard_dex_file.cc',
    'dex/type_lookup_table.cc',
    'dex/utf.cc',
  ],
  include_directories: libdexfile_includes,
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [
    libbase_dep,
    liblog_dep,
    libartbase_dep,
    libnativehelper_dep,
    libartpalette_dep,
    libz_dep,
  ],
  install: true,
)

libdexfile_dep = declare_dependency(include_directories: libdexfile_includes, link_with: libdexfile)
