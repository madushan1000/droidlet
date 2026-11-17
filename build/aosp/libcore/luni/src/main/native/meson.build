libandroidio = shared_library('androidio', 
  'AsynchronousCloseMonitor.cpp',
  cpp_args: ['-std=c++20'],
  dependencies: [
    liblog_dep,
  ],
  install: true
)

libandroidio_dep = declare_dependency(link_with: libandroidio)

libjavacore_includes = include_directories('.')

libjavacore = shared_library(
  'javacore',
  [
    'ExecStrings.cpp',
    'IcuUtilities.cpp',
    'JniConstants.cpp',
    'JniException.cpp',
    'NetworkUtilities.cpp',
    'Register.cpp',
    'android_system_OsConstantsHolder.cpp',
    'cbigint.cpp',
    'java_lang_invoke_MethodHandle.cpp',
    'java_lang_invoke_VarHandle.cpp',
    'libcore_math_NativeBN.cpp',
    'libcore_icu_ICU.cpp',
    'libcore_io_AsynchronousCloseMonitor.cpp',
    'libcore_io_Linux.cpp',
    'libcore_io_Memory.cpp',
    'libcore_util_NativeAllocationRegistry.cpp',
    'org_apache_harmony_xml_ExpatParser.cpp',
  ],
  cpp_args: ['-std=c++20'],
  link_with: libandroidio,
  dependencies: [
    libnativehelper_dep,
    liblog_dep,
    libbase_dep,
    libcrypto_dep,
    dependency('icu-i18n'),
    dependency('expat')
  ],
  include_directories: libjavacore_includes,
  install: true,
)

libjavacore_dep = declare_dependency(
  include_directories: libjavacore_includes,
  link_with: libjavacore,
)
