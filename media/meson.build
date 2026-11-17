# Host-side android.media JNI backend.  The stock framework classes load
# "media_jni"; this library implements the MediaMetadataRetriever entry points
# with host ffmpeg (including VAAPI/Vulkan-capable builds) instead of the
# platform media stack.
libmedia_jni = shared_library(
  'media_jni',
  'media_jni.cpp',
  'media_player.cpp',
  'audio_track.cpp',
  'audio_sink.cpp',
  'media_extractor.cpp',
  'media_codec.cpp',
  'media_codec_list.cpp',
  'codec_registry.cpp',
  include_directories: [
    libsystem_includes,
    frameworks_native_includes,
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libnativehelper_dep,
    liblog_dep,
    libnativewindow_dep,
    dependency('libavformat'),
    dependency('libavcodec'),
    dependency('libavutil'),
    dependency('libswscale'),
    dependency('libswresample'),
    dependency('alsa'),
    dependency('libpulse-simple'),
  ],
  install: true,
)

# android.media.SoundPool's static initializer does
# System.loadLibrary("soundpool"); a missing library makes the class fail to
# initialize and takes down apps that create a SoundPool during startup.
# Playback is silent, but setup/load/play succeed and loads report completion.
libsoundpool = shared_library(
  'soundpool',
  'soundpool_stub.cpp',
  include_directories: [
    libsystem_includes,
    frameworks_native_includes,
  ],
  cpp_args: ['-std=c++20'],
  dependencies: [
    libnativehelper_dep,
    liblog_dep,
  ],
  install: true,
)
