executable(
  'app_process',
  [
    'app_main.cpp',
  ],
  include_directories: [
    system_core_includes,
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libbase_dep,
    libbinder_dep,
    libcutils_dep,
    libutils_dep,
    liblog_dep,
    libandroid_runtime_dep,
    libnativehelper_dep,
  ],
  link_with: bt_dl_bio,
  export_dynamic: true,
  install: true,
)
