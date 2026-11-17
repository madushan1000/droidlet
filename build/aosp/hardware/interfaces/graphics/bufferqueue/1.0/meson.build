android_hardware_graphics_bufferqueue_1_0 = custom_target(
  'android_hardware_graphics_bufferqueue_1_0',
  build_by_default: true,
  input: [
    'IGraphicBufferProducer.hal',
    'IProducerListener.hal',
  ],
  output: [
    'android/hardware/graphics/bufferqueue/1.0/BnHwGraphicBufferProducer.h',
    'android/hardware/graphics/bufferqueue/1.0/BnHwProducerListener.h',
    'android/hardware/graphics/bufferqueue/1.0/BpHwGraphicBufferProducer.h',
    'android/hardware/graphics/bufferqueue/1.0/BpHwProducerListener.h',
    'android/hardware/graphics/bufferqueue/1.0/BsGraphicBufferProducer.h',
    'android/hardware/graphics/bufferqueue/1.0/BsProducerListener.h',
    'android/hardware/graphics/bufferqueue/1.0/IGraphicBufferProducer.h',
    'android/hardware/graphics/bufferqueue/1.0/IHwGraphicBufferProducer.h',
    'android/hardware/graphics/bufferqueue/1.0/IHwProducerListener.h',
    'android/hardware/graphics/bufferqueue/1.0/IProducerListener.h',
    'android/hardware/graphics/bufferqueue/1.0/GraphicBufferProducerAll.cpp',
    'android/hardware/graphics/bufferqueue/1.0/ProducerListenerAll.cpp',
  ],
  command: [
    hidl_gen,
    '-R',
    '-L', 'c++',
    '-randroid.hardware:'
    + meson.project_source_root() / 'hardware/interfaces',
    '-randroid.hidl:' + system_libhidl_transport_root, 'android.hardware.graphics.bufferqueue@1.0',
    '-o', meson.current_build_dir(),
  ],
)

libandroid_hardware_graphics_bufferqueue_1_0 = static_library(
  'android_hardware_graphics_bufferqueue_1_0',
  android_hardware_graphics_bufferqueue_1_0,
  include_directories: [system_core_includes],
  dependencies: [
    libutils_dep,
    libcutils_dep,
    liblog_dep,
    libbase_dep,
    libhidl_dep,
    libfmq_dep,
    #libhardware_dep,
    libhwbinder_dep,
    libandroid_hardware_graphics_common_1_0_dep,
    libandroid_hardware_media_1_0_dep,
  ],
)

libandroid_hardware_graphics_bufferqueue_1_0_dep = declare_dependency(
  include_directories: '.',
  link_with: libandroid_hardware_graphics_bufferqueue_1_0,
)
