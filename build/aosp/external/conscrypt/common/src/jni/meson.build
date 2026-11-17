libjavacrypto_includes = include_directories('main/include')
libjavacrypto = shared_library(
  'javacrypto',
  [
    'main/cpp/conscrypt/compatibility_close_monitor.cc',
    'main/cpp/conscrypt/jniload.cc',
    'main/cpp/conscrypt/jniutil.cc',
    'main/cpp/conscrypt/native_crypto.cc',
    'main/cpp/conscrypt/netutil.cc',
  ],
  include_directories: libjavacrypto_includes,
  cpp_args: [
    '-std=c++20',
    '-DANDROID',
    '-DJNI_JARJAR_PREFIX=com/android/',
  ],
  dependencies: [
    libnativehelper_dep,
    libcrypto_dep,
    libssl_dep,
    liblog_dep,
  ],
  install: true,
)

libjavacrypto_dep = declare_dependency(
  include_directories: libjavacrypto_includes,
  link_with: libjavacrypto,
)
