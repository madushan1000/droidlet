# Default system properties.  main.cpp reads them from $PREFIX/etc before the
# VM starts; HOST_PROPERTIES overrides individual values at run time.
install_data(
  'droidlet.prop',
  install_dir: 'etc',
)

executable(
  'droidlet',
  [
    'main.cpp',
  ],
  include_directories: [
    system_core_includes,
    '../presenter',
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
  link_with: [droidlet_presenter, bt_dl_bio],
  export_dynamic: true,
  install: true,
)
