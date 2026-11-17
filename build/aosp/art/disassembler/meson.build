libart_disassembler_includes = include_directories('.')

libart_disassembler_srcs = ['disassembler.cc']

if target_machine.cpu() == 'x86_64'
  libart_disassembler_srcs += ['disassembler_x86.cc']
endif

libart_disassembler = static_library(
  'libart-disassembler',
  libart_disassembler_srcs,
  cpp_args: art_default_cflags + ['-std=c++26'],
  dependencies: [libbase_dep, libartbase_dep],
)

libart_disassembler_dep = declare_dependency(
  include_directories: libart_disassembler_includes,
  link_with: libart_disassembler,
)
