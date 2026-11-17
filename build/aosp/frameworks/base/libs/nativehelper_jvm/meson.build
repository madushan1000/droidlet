libnativehelper_jvm_includes = include_directories('.')

libnativehelper_jvm = static_library(
  'nativehelper_jvm',
  [
    'JNIPlatformHelp.c',
    'JniConstants.c',
    'file_descriptor_jni.c',
  ],
  dependencies: [
    libnativehelper_dep,
    liblog_dep,
  ],
)

libnativehelper_jvm_dep = declare_dependency(
  include_directories: libnativehelper_jvm_includes,
  link_with: libnativehelper_jvm,
)
