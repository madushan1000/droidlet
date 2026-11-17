asm_defines_lib = static_library(
  'asm_defines_lib',
  [
    'asm_defines.cc',
  ],
  include_directories: [libart_runtime_includes],
  cpp_args: art_default_cflags + ['-S', '-std=c++20'],
  dependencies: [
    libartbase_dep,
    libbase_dep,
    libdexfile_dep,
    libnativehelper_dep,
    com_android_art_flags_cpp_dep,
  ],
)

asm_defines_h = custom_target(
  'asm_defines_h',
  input: [
    asm_defines_lib.extract_objects('asm_defines.cc'),
  ],
  output: ['asm_defines.h'],
  capture: true,
  command: [
    python,
    '@SOURCE_ROOT@/art/tools/cpp-define-generator/make_header.py',
    '@INPUT@',
  ],
  depends: [asm_defines_lib],
)

asm_defines_dep = declare_dependency(include_directories: ['.'], sources: asm_defines_h)
