libnativedisplay_includes = include_directories('include', 'include-private')

libnativedisplay = shared_library(
  'nativedisplay',
  [
    'AChoreographer.cpp',
    'ADisplay.cpp',
    'surfacetexture/surface_texture.cpp',
    'surfacetexture/SurfaceTexture.cpp',
    'surfacetexture/ImageConsumer.cpp',
    'surfacetexture/EGLConsumer.cpp',
    'surfacetexture/LegacySurfaceTexture.cpp',
    'surfacetexture/LegacyImageConsumer.cpp',
    'surfacetexture/LegacyEGLConsumer.cpp',
  ],
  include_directories: [
    libnativedisplay_includes,
    frameworks_native_includes,
    frameworks_native_libs_math_includes,
    system_core_includes,
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libbase_dep,
    libcutils_dep,
    libutils_dep,
    liblog_dep,
    libnativewindow_dep,
    libui_dep,
    libbinder_dep,
    libbinder_ndk_dep,
    libnativehelper_dep,
    libgui_dep,
    dependency('GL'),
    dependency('egl'),
    dependency('epoxy'),
  ],
  install: true,
)

libnativedisplay_dep = declare_dependency(include_directories: libnativedisplay_includes, link_with: libnativedisplay)
