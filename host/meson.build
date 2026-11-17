# Host implementations extracted from the AOSP trees.
#
# These are droidlet-owned static libraries that the AOSP components link
# against.  The AOSP sources keep only the hooks (calls, #ifdef guards and
# include changes) that select these implementations.

host_root = meson.current_source_dir()

# --- EGL window-surface bridge -------------------------------------------
droidlet_host_egl_includes = include_directories('egl')
droidlet_host_egl = static_library(
  'droidlet_host_egl',
  'egl/host_egl_window.cpp',
  include_directories: [
    droidlet_host_egl_includes,
    frameworks_native_includes,
    frameworks_native_libs_math_includes,
    include_directories('../../frameworks/native/libs/nativewindow/include'),
    include_directories('../../frameworks/native/libs/nativebase/include'),
    include_directories('../../frameworks/native/libs/arect/include'),
    include_directories('../../frameworks/native/libs/ui/include'),
    include_directories('../../frameworks/native/libs/gui/include'),
    system_core_includes,
    libcutils_includes,
    libbase_includes,
    include_directories('../../system/logging/liblog/include'),
    bionic_kernel_includes,
  ],
  cpp_args: ['-std=c++20', '-D__INTRODUCED_IN(x)=', '-DDROIDLET_HOST=1'],
  dependencies: [
    droidlet_wayland_dep,
    dependency('egl'),
    dependency('wayland-client'),
    dependency('wayland-egl'),
  ],
)
droidlet_host_egl_dep = declare_dependency(
  include_directories: droidlet_host_egl_includes,
  link_with: droidlet_host_egl,
)

# --- Vulkan Android surface / swapchain bridge ---------------------------
droidlet_host_vulkan = static_library(
  'droidlet_host_vulkan',
  'vulkan/host_vulkan_android.cpp',
  include_directories: [
    include_directories('../../frameworks/native/libs/nativewindow/include'),
    include_directories('../../frameworks/native/libs/arect/include'),
    include_directories('../../external/skia/include/third_party/vulkan'),
  ],
  cpp_args: ['-std=c++20', '-D__INTRODUCED_IN(x)=', '-DDROIDLET_HOST=1'],
  dependencies: [droidlet_wayland_dep, dependency('vulkan')],
)
droidlet_host_vulkan_dep = declare_dependency(link_whole: droidlet_host_vulkan)

# --- GLES1 OES entry points used by the legacy GLImpl JNI ----------------
droidlet_host_gl_oes = static_library(
  'droidlet_host_gl_oes',
  'gl/gl_oes.cpp',
  cpp_args: ['-std=c++20', '-DGL_GLEXT_PROTOTYPES'],
  dependencies: [dependency('egl'), dependency('GL')],
)
droidlet_host_gl_oes_dep = declare_dependency(link_with: droidlet_host_gl_oes)

# --- Perfetto JNI stubs ---------------------------------------------------
droidlet_host_perfetto_includes = include_directories('perfetto')
droidlet_host_perfetto = static_library(
  'droidlet_host_perfetto',
  'perfetto/perfetto_stubs.cpp',
  include_directories: [
    droidlet_host_perfetto_includes,
    frameworks_base_core_jni,
    include_directories('../../libnativehelper/include_jni'),
    include_directories('../../system/logging/liblog/include'),
    include_directories('../../frameworks/base/core/jni/include'),
    include_directories('../../system/core/libutils/include'),
    include_directories('../../system/core/libsystem/include'),
  ],
  cpp_args: ['-std=c++20', '-D__INTRODUCED_IN(x)='],
  dependencies: [libnativehelper_dep],
)
droidlet_host_perfetto_dep = declare_dependency(
  include_directories: droidlet_host_perfetto_includes,
  link_with: droidlet_host_perfetto,
)

# --- NDK media stubs (libmediandk) ---------------------------------------
subdir('media/ndk')

# --- NDK sensor stubs -----------------------------------------------------
droidlet_host_sensors = static_library(
  'droidlet_host_sensors',
  'sensors/sensor_stub.cpp',
  include_directories: [frameworks_native_includes],
  cpp_args: ['-std=c++20', '-D__INTRODUCED_IN(x)='],
)
droidlet_host_sensors_dep = declare_dependency(link_whole: droidlet_host_sensors)

# --- linker namespace stubs ----------------------------------------------
droidlet_host_linker_includes = include_directories('linker/include')
droidlet_host_linker = static_library(
  'droidlet_host_linker',
  'linker/droidlet_host_dlext.cpp',
  include_directories: [
    droidlet_host_linker_includes,
    include_directories('../../bionic/libc/platform'),
  ],
  cpp_args: ['-std=c++20'],
)
droidlet_host_linker_dep = declare_dependency(
  include_directories: droidlet_host_linker_includes,
  link_with: droidlet_host_linker,
)

# --- OpenSL ES host compatibility helpers --------------------------------
droidlet_host_opensles_includes = include_directories('audio')
droidlet_host_opensles_compat_header = host_root / 'audio/host_opensles_compat.h'
droidlet_host_opensles = static_library(
  'droidlet_host_opensles',
  'audio/host_opensles_compat.cpp',
  include_directories: [
    droidlet_host_opensles_includes,
    include_directories('../../frameworks/wilhelm/include'),
    include_directories('../../libnativehelper/include_jni'),
    include_directories('../../system/logging/liblog/include'),
  ],
  c_args: ['-std=c11'],
)
droidlet_host_opensles_dep = declare_dependency(
  include_directories: droidlet_host_opensles_includes,
  link_with: droidlet_host_opensles,
)

# --- ICU bridge header only ----------------------------------------------
droidlet_host_icu_includes = include_directories('icu')

# --- libnativeloader host helpers ---------------------------------------
droidlet_host_native_loader = static_library(
  'droidlet_host_native_loader',
  'runtime/native_loader_host.cpp',
  include_directories: [
    include_directories('../../art/libnativeloader/include'),
    include_directories('../../libnativehelper/include_jni'),
  ],
  cpp_args: ['-std=c++20'],
)
droidlet_host_native_loader_dep = declare_dependency(link_whole: droidlet_host_native_loader)
