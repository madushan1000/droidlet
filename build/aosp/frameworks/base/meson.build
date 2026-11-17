android_content_res_flags_cpp = custom_target(
  input: 'core/java/android/content/res/flags.aconfig',
  command: [
    gen_aconfig_cpp_stubs,
    '@INPUT@',
    'android.content.res',
  ],
  capture: true,
  output: 'android_content_res.h',
)
android_content_res_cpp_dep = declare_dependency(
  sources: [android_content_res_flags_cpp],
  include_directories: include_directories('.'),
)

android_text_flags_cpp = custom_target(
  input: 'core/java/android/text/flags/flags.aconfig',
  command: [
    gen_aconfig_cpp_stubs,
    '@INPUT@',
    'com.android.text.flags',
  ],
  capture: true,
  output: 'com_android_text_flags.h',
)
android_text_flags_cpp_dep = declare_dependency(
  sources: [android_text_flags_cpp],
  include_directories: include_directories('.'),
)
