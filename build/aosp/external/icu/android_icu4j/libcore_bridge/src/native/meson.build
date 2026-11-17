libicu_jni_includes = include_directories('.')

libicu_jni = shared_library(
  'icu_jni',
  [
    'com_android_i18n_timezone_internal_Memory.cpp',
    'com_android_i18n_util_ATrace.cpp',
    'com_android_i18n_util_Log.cpp',
    'com_android_icu_charset_NativeConverter.cpp',
    'com_android_icu_text_TimeZoneNamesNative.cpp',
    'com_android_icu_util_CaseMapperNative.cpp',
    'com_android_icu_util_ICU4CMetadata.cpp',
    'com_android_icu_util_LocaleNative.cpp',
    'com_android_icu_util_regex_MatcherNative.cpp',
    'com_android_icu_util_regex_PatternNative.cpp',
    'com_android_icu_util_UResourceBundleNative.cpp',
    'IcuUtilities.cpp',
    'JniConstants.cpp',
    'MatcherState.cpp',
    'Register.cpp',
  ],
  include_directories: [
    libicu_jni_includes,
    droidlet_host_icu_includes,
    #'../../../../icu4c/source/common',
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libnativehelper_dep,
    liblog_dep,
    libbase_dep,
    libcutils_dep,
    dependency('icu-i18n'),
  ],
  install: true,
)

libicu_jni_dep = declare_dependency(
  include_directories: libicu_jni_includes,
  link_with: libicu_jni,
)
