project(
  'droidlet',
  ['c', 'cpp', 'java'],
  meson_version: '>=1.13',
  default_options: {
    'rust_nightly': 'enabled',
    'buildtype': 'debugoptimized',
  },
)

fs = import('fs')

cc = meson.get_compiler('c')
libz_dep = cc.find_library('libz')
libzstd_dep = cc.find_library('zstd')
tinyxml2_dep = cc.find_library('tinyxml2')
libfmt_dep = cc.find_library('fmt')
lz4_dep = cc.find_library('lz4')
#freetype_dep = cc.find_library('freetype')

droidlet_includes = include_directories('droidlet/include')
libcutils_includes = include_directories('system/core/libcutils/include')
libbase_includes = include_directories('system/libbase/include')
system_core_includes = include_directories('system/core/include')
#system_media_audio_includes = include_directories('system/media/audio/include')
libart_runtime_includes = include_directories('art/runtime')
frameworks_native_includes = include_directories('frameworks/native/include')
frameworks_native_libs_math_includes = include_directories('frameworks/native/libs/math/include')
frameworks_native_private_includes = include_directories('frameworks/native/include/private')
libsystem_includes = include_directories('system/core/libsystem/include')
hardware_graphics_mapper_inclueds = include_directories('hardware/interfaces/graphics/mapper/stable-c/include')
bionic_kernel_includes = include_directories('bionic/libc/kernel/uapi')
frameworks_base_core_jni = include_directories('frameworks/base/core/jni')
#bionic_includes = include_directories('bionic/libc/include/sys')

flatc = find_program('flatc')
zipmerge = find_program('zipmerge')

android_home = get_option('android_home')
if android_home == ''
  error('Set -Dandroid_home=/path/to/android-sdk')
endif
android_build_tools_37 = android_home / 'build-tools/37.0.0'
if not fs.exists(android_build_tools_37) 
  error('No build-tools found under ' + android_build_tools_37)
endif

aidl = find_program('aidl', dirs: android_build_tools_37)
d8 = find_program('d8', dirs: android_build_tools_37)
java = find_program('java')

bionic_translation = subproject('bionic_translation')
bt_dl_bio = bionic_translation.get_variable('dl_bio')

subdir('droidlet/tools')
subdir('system/logging/liblog')
subdir('system/libbase')
subdir('system/core/libcutils')
subdir('libnativehelper')
subdir('droidlet/host/jni_stubs')
subdir('system/libziparchive')
subdir('external/dlmalloc')
subdir('system/libprocinfo')
subdir('external/lzma')
subdir('system/unwinding/libunwindstack')
subdir('system/core/libutils')
subdir('external/cpu_features')
subdir('external/boringssl')
subdir('droidlet/host')

subdir('art')
subdir('art/build/flags')
subdir('art/libartbase')
subdir('art/libartpalette')
subdir('art/libdexfile')
subdir('art/dalvikvm')
subdir('art/sigchainlib')
subdir('art/libprofile')
subdir('art/cmdline')
subdir('art/libelffile')
subdir('art/libnativebridge')
subdir('art/libnativeloader')
subdir('art/disassembler')
subdir('art/compiler')
subdir('art/tools/cpp-define-generator')
subdir('art/runtime')
subdir('art/openjdkjvm')

subdir('system/core/libsync')



subdir('frameworks/native/libs/android_runtime_lazy')
subdir('frameworks/native/libs/binder')
subdir('frameworks/native/libs/binder/ndk')

subdir('hardware/interfaces/common/aidl')
subdir('hardware/interfaces/graphics/common/aidl')
subdir('hardware/interfaces/graphics/mapper/stable-c')
subdir('hardware/interfaces/common/support')
subdir('droidlet/host/gralloc')


subdir('frameworks/native/libs/ui')

subdir('external/wuffs-mirror-release-c')
subdir('frameworks/native/libs/nativewindow')
subdir('external/skia')
subdir('droidlet/host/hwui')
subdir('frameworks/wilhelm')

subdir('droidlet/host/incfs_stub')
subdir('frameworks/base')
subdir('frameworks/base/libs/androidfw')
subdir('frameworks/native')


subdir('frameworks/native/libs/input')


subdir('frameworks/native/services/surfaceflinger')
subdir('frameworks/native/libs/gui')
subdir('frameworks/minikin/libs/minikin')

subdir('frameworks/native/libs/nativedisplay')
subdir('frameworks/native/libs/ipcrenderbuffer')
subdir('frameworks/native/libs/graphicsenv')
subdir('hardware/interfaces/drm/common/aidl')
subdir('hardware/interfaces/graphics/composer/aidl')
subdir('frameworks/native/libs/tonemap')
subdir('frameworks/native/libs/shaders')
subdir('frameworks/base/libs/hwui')
subdir('frameworks/base/native/graphics/jni')

subdir('external/sqlite')

subdir('system/memory/libmeminfo')
subdir('droidlet/host/processgroup_stub')

subdir('frameworks/base/libs/nativehelper_jvm')
subdir('frameworks/base/core/jni')
subdir('frameworks/base/cmds/app_process')
subdir('droidlet/presenter')
subdir('droidlet/media')
subdir('droidlet/droidlet')
subdir('droidlet/prebuilts')
subdir('droidlet/framework_overlay')

subdir('external/icu/android_icu4j/libcore_bridge/src/native')
subdir('libcore/luni/src/main/native')
subdir('external/fdlibm')
subdir('libcore/ojluni/src/main/native')
subdir('frameworks/base/native/android')
subdir('external/conscrypt/common/src/jni')

subdir('frameworks/libs/modules-utils')
subdir('tools/platform-compat')
subdir('external/okhttp')
subdir('external/icu')
subdir('external/icu/android_icu4j')
subdir('droidlet/java_stubs')
subdir('libcore')
subdir('external/bouncycastle')
subdir('external/apache-xml')
subdir('external/flatbuffers')
subdir('external/conscrypt')

subdir('frameworks/base/data')
subdir('external/robolectric/nativeruntime/src/main/resources')
