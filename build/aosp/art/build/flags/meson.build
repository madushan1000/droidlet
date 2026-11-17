com_android_art_rw_flags_cpp = custom_target(
  input: 'art-rw-flags.aconfig',
  command: [
    gen_aconfig_cpp_stubs,
    '@INPUT@',
    'com.android.art.rw.flags',
  ],
  capture: true,
  output: 'com_android_art_rw_flags.h',
)
com_android_art_rw_flags_cpp_dep = declare_dependency(
  sources: [com_android_art_rw_flags_cpp],
  include_directories: include_directories('.'),
)

com_android_art_flags_cpp = custom_target(
  input: 'art-flags.aconfig',
  command: [
    gen_aconfig_cpp_stubs,
    '@INPUT@',
    'com.android.art.flags',
  ],
  capture: true,
  output: 'com_android_art_flags.h',
)
com_android_art_flags_cpp_dep = declare_dependency(
  sources: [com_android_art_flags_cpp],
  include_directories: include_directories('.'),
)

com_android_art_flags_java = custom_target(
  capture: true,
  input: ['art-flags.aconfig'],
  output: ['com/android/art/flags/Flags.java'],
  command: [
    gen_aconfig_java_stubs,
    '@INPUT@',
  ],
)

com_android_art_flags_module_path = ':' + meson.current_build_dir()
