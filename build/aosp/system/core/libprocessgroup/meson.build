libprocessgroup_includes = include_directories(
  'include',
  'include_platform',
  'util/include',
  'setup/include',
)

libprocessgroup = shared_library(
  'processgroup',
  [
    'cgroup_map.cpp',
    'processgroup.cpp',
    'sched_policy.cpp',
    'task_profiles.cpp',
    'util/cgroup_controller.cpp',
    'util/util.cpp',
    'setup/cgroup_map_write.cpp'
  ],
  include_directories: libprocessgroup_includes,
  cpp_args: ['-std=c++23'],
  dependencies: [
    libbase_dep,
    libcutils_dep,
    libutils_dep,
    dependency('jsoncpp'),
  ],
  install: true,
)

libprocessgroup_dep = declare_dependency(
  include_directories: libprocessgroup_includes,
  link_with: libprocessgroup,
)
