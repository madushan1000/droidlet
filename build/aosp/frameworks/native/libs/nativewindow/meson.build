libnativewindow_includes = include_directories(
  'include',
  '../arect/include',
  '../nativebase/include',
)

libnativewindow = shared_library(
  'nativewindow',
  [
    'AHardwareBuffer.cpp',
    'ANativeWindow.cpp',
  ],
  include_directories: [
    libnativewindow_includes,
    system_core_includes,
    '../math/include',
    'include-private',
  ],
  cpp_args: [
    '-D__ANDROID_API_S__=31',
    '-D__ANDROID_API_U__=34',
  ],
  dependencies: [
    libbase_dep,
    libutils_dep,
    libcutils_dep,
    liblog_dep,
    libbinder_dep,
    libbinder_ndk_dep,
    libui_dep,
  ],
  install: true,
)

libnativewindow_dep = declare_dependency(
  include_directories: [libnativewindow_includes, 'include-private'],
  link_with: libnativewindow,
)
