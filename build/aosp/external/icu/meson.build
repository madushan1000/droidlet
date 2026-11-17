com_android_icu_flags_java = custom_target(
  input: 'icu.aconfig',
  output: 'com/android/icu/Flags.java',
  capture: true,
  command: [
    gen_aconfig_java_stubs,
    '@INPUT@',
  ],
)

com_android_icu_flags_module_path = ':' + meson.current_build_dir()
