libsqlite3_android_includes = include_directories('android')

libsqlite3_android = static_library(
  'sqlite3_android',
  [
    'android/PhoneNumberUtils.cpp',
    'android/OldPhoneNumberUtils.cpp',
    'android/sqlite3_android.cpp',
  ],
  include_directories: libsqlite3_android_includes,
  cpp_args: [
    '-std=c++20',
    '-DSQLITE_ENABLE_ICU',
    '-DSQLITE_DEFAULT_LEGACY_ALTER_TABLE',
  ],
  dependencies: [
    liblog_dep,
    dependency('sqlite3'),
    dependency('icu-i18n'),
  ],
)

libsqlite3_android_dep = declare_dependency(
  include_directories: libsqlite3_android_includes,
  link_with: libsqlite3_android,
)
