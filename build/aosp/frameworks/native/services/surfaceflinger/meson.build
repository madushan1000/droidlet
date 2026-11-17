com_android_graphics_surfaceflinger_flags_cpp = custom_target(
  input: 'surfaceflinger_flags_new.aconfig',
  command: [
    gen_aconfig_cpp_stubs,
    '@INPUT@',
    'com.android.graphics.surfaceflinger.flags',
  ],
  capture: true,
  output: 'com_android_graphics_surfaceflinger_flags.h',
)
com_android_graphics_surfaceflinger_flags_cpp_dep = declare_dependency(
  sources: [com_android_graphics_surfaceflinger_flags_cpp],
  include_directories: include_directories('.'),
)
