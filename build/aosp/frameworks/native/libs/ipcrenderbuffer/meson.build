libipcrenderbuffer_includes = include_directories('include')
libipcrenderbuffer = static_library(
  'ipcrenderbuffer',
  [
    'src/RenderBufferOps.cpp',
    'src/RenderBufferDebugUtils.cpp',
    'src/IPCRecordingCanvas.cpp',
    '../gui/RenderCommandBuffer.cpp',
    '../gui/RenderCommandBufferConsumer.cpp',
    '../gui/RenderCommandBufferProducer.cpp',
  ],
  include_directories: [
    libipcrenderbuffer_includes,
    frameworks_native_includes,
    frameworks_native_libs_math_includes,
    system_core_includes,
    '../arect/include',
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libbase_dep,
    libskia_dep,
    liblog_dep,
    libui_dep,
    libgui_dep,
    libutils_dep,
    libcutils_dep,
    libbinder_dep,
    libbinder_ndk_dep,
    libnativewindow_dep,
    libnativehelper_dep,
  ],
)

libipcrenderbuffer_dep = declare_dependency(
  include_directories: libipcrenderbuffer_includes,
  link_with: libipcrenderbuffer,
)
