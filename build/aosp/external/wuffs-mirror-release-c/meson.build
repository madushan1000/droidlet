libwuffs_mirror_release_c = static_library(
  'wuffs-mirror-release-c',
  [
    'release/c/wuffs-v0.3.c',
  ],
  c_args: [
    '-DWUFFS_IMPLEMENTATION',
    '-DWUFFS_CONFIG__MODULES',
    '-DWUFFS_CONFIG__MODULE__BASE',
    '-DWUFFS_CONFIG__MODULE__GIF',
    '-DWUFFS_CONFIG__MODULE__LZW',
  ],
)
libwuffs_mirror_release_c_dep = declare_dependency(
  link_with: libwuffs_mirror_release_c,
  include_directories: ['release/c'],
)
