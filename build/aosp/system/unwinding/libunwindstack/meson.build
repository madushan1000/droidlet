libunwindstack_includes = include_directories('include')
libunwindstack_srcs = [
  'AndroidUnwinder.cpp',
  'ArmExidx.cpp',
  'Demangle.cpp',
  'DexFiles.cpp',
  'DwarfCfa.cpp',
  'DwarfEhFrameWithHdr.cpp',
  'DwarfMemory.cpp',
  'DwarfOp.cpp',
  'DwarfSection.cpp',
  'Elf.cpp',
  'ElfInterface.cpp',
  'ElfInterfaceArm.cpp',
  'Global.cpp',
  'JitDebug.cpp',
  'MapInfo.cpp',
  'Maps.cpp',
  'Memory.cpp',
  'MemoryMte.cpp',
  'MemoryXz.cpp',
  'Regs.cpp',
  'RegsArm.cpp',
  'RegsArm64.cpp',
  'RegsX86.cpp',
  'RegsX86_64.cpp',
  'RegsRiscv64.cpp',
  'Symbols.cpp',
  'ThreadEntry.cpp',
  'ThreadUnwinder.cpp',
  'Unwinder.cpp',

  #'DexFile.cpp',
  'LogAndroid.cpp',
]

if target_machine.cpu() == 'x86_64'
  libunwindstack_srcs += ['AsmGetRegsX86_64.S']
endif

libunwindstack = shared_library(
  'unwindstack',
  libunwindstack_srcs,
  include_directories: [libunwindstack_includes, droidlet_includes],
  cpp_args: [
    '-std=c++20',
    #'-UDEXFILE_SUPPORT',
  ],
  dependencies: [
    libbase_dep,
    libprocinfo_dep,
    liblog_dep,
    liblzma_dep,
    libz_dep,
    libzstd_dep,
  ],
  install: true,
)

libunwindstack_dep = declare_dependency(include_directories: libunwindstack_includes, link_with: libunwindstack)
