aidl_input_cpp = custom_target(
  'aidl_input',
  input: [
    'android/os/IInputConstants.aidl',
    'android/os/InputConfig.aidl',
    'android/os/InputEventInjectionResult.aidl',
    'android/os/InputEventInjectionSync.aidl',
    'android/os/MotionEventFlag.aidl',
    'android/os/PointerCaptureMode.aidl',
    'android/os/PointerIconType.aidl',
    #'android/os/IInputFlinger.aidl',
    'android/os/InputChannelCore.aidl',
  ],
  output: [
    'android/os/BnInputConfig.h',
    'android/os/BnInputConstants.h',
    'android/os/BnInputEventInjectionResult.h',
    'android/os/BnInputEventInjectionSync.h',
    'android/os/BnMotionEventFlag.h',
    'android/os/BnPointerCaptureMode.h',
    'android/os/BnPointerIconType.h',
    'android/os/BpInputConfig.h',
    'android/os/BpInputConstants.h',
    'android/os/BpInputEventInjectionResult.h',
    'android/os/BpInputEventInjectionSync.h',
    'android/os/BpMotionEventFlag.h',
    'android/os/BpPointerCaptureMode.h',
    'android/os/BpPointerIconType.h',
    'android/os/IInputConstants.h',
    'android/os/InputConfig.h',
    'android/os/InputEventInjectionResult.h',
    'android/os/InputEventInjectionSync.h',
    'android/os/MotionEventFlag.h',
    'android/os/PointerCaptureMode.h',
    'android/os/PointerIconType.h',
    'android/os/BnInputChannelCore.h',
    'android/os/BpInputChannelCore.h',
    'android/os/InputChannelCore.h',
    'android/os/InputChannelCore.cpp',
    'android/os/IInputConstants.cpp',
    'android/os/InputConfig.cpp',
    'android/os/InputEventInjectionResult.cpp',
    'android/os/InputEventInjectionSync.cpp',
    'android/os/MotionEventFlag.cpp',
    'android/os/PointerCaptureMode.cpp',
    'android/os/PointerIconType.cpp',
  ],
  build_by_default: true,
  command: [
    aidl,
    '--lang=cpp',
    '-I', fs.relative_to(meson.current_source_dir(), meson.project_build_root()),
    #'-I', aidl_android_hardware_common_path,
    #'--stability=vintf',
    #'--structured',
    '-o', meson.current_build_dir(),
    '-h', meson.current_build_dir(),
    '@INPUT@',
  ],
)

libaidl_input = static_library(
  'aidl_input',
  aidl_input_cpp,
  cpp_args: ['-std=c++20'],
  dependencies: [
    #libaidl_android_hardware_common_dep,
    libbinder_dep,
    libbase_dep,
    libutils_dep,
    libcutils_dep,
    liblog_dep,
  ],
)

libaidl_input_dep = declare_dependency(
  include_directories: ['.', '..'],
  link_with: libaidl_input,
)

com_android_input_flags_cpp = custom_target(
  input: 'input_flags.aconfig',
  command: [
    gen_aconfig_cpp_stubs,
    '@INPUT@',
    'com.android.input.flags',
  ],
  capture: true,
  output: 'com_android_input_flags.h',
)
com_android_input_flags_cpp_dep = declare_dependency(
  sources: [com_android_input_flags_cpp],
  include_directories: include_directories('.'),
)

input_h_labes_h = custom_target(
  'input_h_labes_h',
  input: [
    meson.project_source_root() / 'bionic/libc/kernel/uapi/linux/input.h',
    meson.project_source_root() / 'bionic/libc/kernel/uapi/linux/input-event-codes.h',
  ],
  output: ['input.h-labels.h'],
  command: [
    meson.project_source_root() / 'system/core/toolbox/generate-input.h-labels.py',
    '@INPUT@',
  ],
  capture: true,
)
libinput_includes = include_directories('.')

libinput = shared_library(
  'input',
  [
    input_h_labes_h,
    'AccelerationCurve.cpp',
    'CoordinateFilter.cpp',
    'DisplayTopologyGraph.cpp',
    'Input.cpp',
    'InputConsumer.cpp',
    'InputConsumerNoResampling.cpp',
    'InputDevice.cpp',
    'InputEventLabels.cpp',
    'InputTransport.cpp',
    #'InputVerifier.cpp',
    'KeyCharacterMap.cpp',
    'KeyLayoutMap.cpp',
    'Keyboard.cpp',
    #'KeyboardClassifier.cpp',
    #'MotionPredictor.cpp',
    #'MotionPredictorMetricsManager.cpp',
    'OneEuroFilter.cpp',
    'PrintTools.cpp',
    'PropertyMap.cpp',
    'Resampler.cpp',
    #'TfLiteMotionPredictor.cpp',
    'TouchVideoFrame.cpp',
    'VelocityControl.cpp',
    'VelocityTracker.cpp',
    'VirtualInputDevice.cpp',
    'VirtualKeyMap.cpp',
  ],
  include_directories: [
    libinput_includes,
    frameworks_native_includes,
    frameworks_native_libs_math_includes,
    system_core_includes,
    bionic_kernel_includes,
    '../arect/include',
  ],
  cpp_args: ['-std=c++23'],
  dependencies: [
    libbase_dep,
    liblog_dep,
    libnativehelper_dep,
    libcutils_dep,
    libutils_dep,
    libbinder_ndk_dep,
    libbinder_dep,
    libui_dep,
    libaidl_input_dep,
    dependency('fmt'),
  ],
  install: true,
)

libinput_dep = declare_dependency(
  include_directories: libinput_includes,
  link_with: libinput,
)
