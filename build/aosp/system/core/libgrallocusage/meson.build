libgrallocusage_includes = include_directories('include')

libgrallocusage = shared_library(
  'grallocusage',
  'GrallocUsageConversion.cpp',
  include_directories: [
    libgrallocusage_includes,
    droidlet_includes,
    libsystem_includes,
  ],
  dependencies: [
    libcutils_dep,
  ],
  install: true,
)

libgrallocusage_dep = declare_dependency(
  include_directories: libgrallocusage_includes,
  link_with: libgrallocusage,
)
