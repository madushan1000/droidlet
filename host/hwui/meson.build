# hwui media/EGL stubs (needs the Skia headers, so this is built after
# external/skia).
# --- hwui media/EGL stubs -------------------------------------------------
droidlet_host_hwui_stubs = static_library(
  'droidlet_host_hwui_stubs',
  'hwui_media_stubs.cpp',
  include_directories: [
    include_directories('../../../frameworks/base/libs/hwui'),
    include_directories('../../../frameworks/av/media/ndk/include'),
    include_directories('../../../frameworks/native/libs/nativewindow/include'),
    include_directories('../../../frameworks/native/libs/arect/include'),
    include_directories('../../../system/logging/liblog/include'),
    include_directories('../../../system/core/libcutils/include'),
    frameworks_native_includes,
    include_directories('../../../system/core/libutils/include'),
  ],
  cpp_args: ['-std=c++20', '-DDROIDLET_HOST=1'],
  dependencies: [libskia_dep, droidlet_host_gralloc_dep],
)
droidlet_host_hwui_stubs_dep = declare_dependency(link_with: droidlet_host_hwui_stubs)

