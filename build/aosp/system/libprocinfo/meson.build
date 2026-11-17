libprocinfo_includes = include_directories('include')

libprocinfo = static_library(
  'procinfo',
  'process.cpp',
  include_directories: libprocinfo_includes,
  dependencies: [libbase_dep]
)

libprocinfo_dep = declare_dependency(include_directories: libprocinfo_includes, link_with: libprocinfo)
