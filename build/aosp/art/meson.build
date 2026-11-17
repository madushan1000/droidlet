#art_default_cflags = [
#  # Base set of cflags used by all things ART.
#  '-fno-rtti',
#  '-ggdb3',
#  #'-Wall',
#  #'-Werror',
#  #'-Wextra',
#  '-Wstrict-aliasing',
#  '-fstrict-aliasing',
#  '-Wunreachable-code',
#  '-Wredundant-decls',
#  '-Wshadow',
#  '-Wunused',
#  #'-fvisibility=protected',
#
#  # Warn about thread safety violations with clang.
#  #'-Wthread-safety',
#  # TODO(b/144045034): turn on -Wthread-safety-negative
#  #'-Wthread-safety-negative',
#
#  # Warn if switch fallthroughs aren't annotated.
#  '-Wimplicit-fallthrough',
#
#  # Enable float equality warnings.
#  '-Wfloat-equal',
#
#  # Enable warning of converting ints to void*.
#  '-Wint-to-void-pointer-cast',
#
#  # Enable warning of wrong unused annotations.
#  '-Wused-but-marked-unused',
#
#  # Enable warning for deprecated language features.
#  '-Wdeprecated',
#
#  # Enable warning for unreachable break & return.
#  '-Wunreachable-code-break',
#  '-Wunreachable-code-return',
#
#  # Disable warning for use of offsetof on non-standard layout type.
#  # We use it to implement OFFSETOF_MEMBER - see macros.h.
#  '-Wno-invalid-offsetof',
#
#  # Enable inconsistent-missing-override warning. This warning is disabled by default in
#  # Android.
#  '-Winconsistent-missing-override',
#
#  # Enable thread annotations for std::mutex, etc.
#  '-D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS',
#  '-mavx2',
#  '-mfma',
#  '-Wmissing-noreturn',
#  '-fno-omit-frame-pointer',
#  '-msse4.2',
#  '-mpopcnt',
#
#  #'-DART_ENABLE_CODEGEN_arm',
#  #'-DART_ENABLE_CODEGEN_arm64',
#  #'-DART_ENABLE_CODEGEN_riscv64',
#  '-DART_ENABLE_CODEGEN_x86',
#  '-DART_ENABLE_CODEGEN_x86_64',
#
#  '-DART_STACK_OVERFLOW_GAP_arm=8192',
#  '-DART_STACK_OVERFLOW_GAP_arm64=8192',
#  '-DART_STACK_OVERFLOW_GAP_riscv64=8192',
#  '-DART_STACK_OVERFLOW_GAP_x86=8192',
#  '-DART_STACK_OVERFLOW_GAP_x86_64=8192',
#
#  '-DART_FRAME_SIZE_LIMIT=1736',
#
#  '-DART_DEFAULT_GC_TYPE_IS_CMC',
#
#  '-DSYS_SECCOMP=1',
#
#  '-DART_BASE_ADDRESS=0x60000000',
#  '-DART_BASE_ADDRESS_MIN_DELTA=(-0x1000000)',
#  '-DART_BASE_ADDRESS_MAX_DELTA=0x1000000',
#
#  '-DART_CLANG_PATH="/usr"',
#  '-DNDEBUG',
#]

art_default_cflags = [
  '-fno-rtti',
  '-fsized-deallocation',
  '-fno-exceptions',
  '-fPIC',
  '-fno-strict-aliasing',
  '-fno-omit-frame-pointer',
  '-fstack-protector-strong',
  '-faddrsig',
  '-ffp-contract=off',
  '-fmessage-length=0',
  '-fdebug-default-version=5',
  '-fno-relaxed-template-template-args',
  '-ftrivial-auto-var-init=zero',
  '-mstackrealign',
  '-msse4.2',
  '-mpopcnt',
  '-m64',
  '-D__compiler_offsetof=__builtin_offsetof',
  '-D_GNU_SOURCE',
  '-DART_DEFAULT_GC_TYPE_IS_CMC',
  '-DART_USE_GENERATIONAL_GC=1',
  '-DART_USE_TLAB=1',
  '-DART_STACK_OVERFLOW_GAP_arm=8192',
  '-DART_STACK_OVERFLOW_GAP_arm64=8192',
  '-DART_STACK_OVERFLOW_GAP_riscv64=8192',
  '-DART_STACK_OVERFLOW_GAP_x86=8192',
  '-DART_STACK_OVERFLOW_GAP_x86_64=8192',
  '-DART_PAGE_SIZE_AGNOSTIC=1',
  '-DUSE_D8_DESUGAR=1',
  '-DART_ENABLE_CODEGEN_x86',
  '-DART_ENABLE_CODEGEN_x86_64',
  '-DSYS_SECCOMP=1',
  '-DART_FRAME_SIZE_LIMIT=1744',
  '-DART_BASE_ADDRESS=0x60000000',
  '-DART_BASE_ADDRESS_MIN_DELTA=(-0x1000000)',
  '-DART_BASE_ADDRESS_MAX_DELTA=0x1000000',
  '-DART_CLANG_PATH="/usr"',
  '-D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS',
  '-Wno-error',
  '-Wno-unused-command-line-argument',
  '-Wno-deprecated-declarations',
  '-Wno-macro-redefined',

  '-DNDEBUG',
]
