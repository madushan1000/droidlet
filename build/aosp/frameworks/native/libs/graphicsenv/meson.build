libgraphicsenv_includes = include_directories('include')

libgraphicsenv = shared_library(
  'graphicsenv',
  [
    'FeatureOverrides.cpp',
    'GpuStatsInfo.cpp',
    'GraphicsEnv.cpp',
    'IGpuService.cpp',
  ],
  include_directories: [libgraphicsenv_includes, include_directories('../../../../bionic/libc/platform')],
  cpp_args: ['-std=c++20'],
  install: true,
  dependencies: [
    droidlet_host_linker_dep,
    libbase_dep,
    liblog_dep,
    libutils_dep,
    libcutils_dep,
    libbinder_dep,
    libbinder_ndk_dep,
  ]
  
)

libgraphicsenv_dep = declare_dependency(
  include_directories: [libgraphicsenv_includes, include_directories('../../../../bionic/libc/platform')],
  link_with: libgraphicsenv,
)
