# Host NDK media stub library.
#
# The apps that link libmediandk get the same symbols as on device, but all
# decoders/drm report AMEDIA_ERROR_UNSUPPORTED; real media playback goes
# through droidlet's own media JNI.

libmediandk = shared_library(
  'mediandk',
  'mediandk_stub.cpp',
  include_directories: [
    include_directories('include'),
    include_directories('../../../../frameworks/av/media/ndk/include'),
    include_directories('../../../../frameworks/native/libs/nativewindow/include'),
    include_directories('../../../../frameworks/native/libs/arect/include'),
    frameworks_native_includes,
  ],
  cpp_args: ['-std=c++20'],
  install: true,
)
