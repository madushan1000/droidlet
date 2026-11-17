libutils_includes = include_directories('include')

libutils = shared_library(
  'utils',
  [
    'FileMap.cpp',
    'JenkinsHash.cpp',
    'LightRefBase.cpp',
    'NativeHandle.cpp',
    'Printer.cpp',
    'StopWatch.cpp',
    'SystemClock.cpp',
    'Threads.cpp',
    'Timers.cpp',
    'Tokenizer.cpp',
    'misc.cpp',

    'Looper.cpp',

    #binder stuff
    'binder/Errors.cpp',
    'binder/RefBase.cpp',
    'binder/SharedBuffer.cpp',
    'binder/String16.cpp',
    'binder/String8.cpp',
    'binder/StrongPointer.cpp',
    'binder/Unicode.cpp',
    'binder/VectorImpl.cpp',
  ],
  include_directories: [system_core_includes, libutils_includes],
  cpp_args: ['-std=c++20', '-D__ANDROID__'],
  dependencies: [liblog_dep, libbase_dep, libcutils_dep],
  install: true,
)

libutils_dep = declare_dependency(include_directories: libutils_includes, link_with: libutils)
