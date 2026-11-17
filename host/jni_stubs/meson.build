# Host replacements for framework JNI libraries.
#
# libstats_jni is loaded eagerly by android.util.StatsLog.  The real library
# requires statsd and binder; this stub drops events instead.

shared_library(
  'stats_jni',
  'stats_jni.cpp',
  include_directories: libnativehelper_includes,
  install: true,
)

# libframework-connectivity-jni is loaded by android.net.NetworkUtils during
# ActivityThread.handleBindApplication() (ConnectivityManager.getDefaultProxy).
# droidlet has no kernel networking, so the stub reports no bound network.
shared_library(
  'framework-connectivity-jni',
  'framework_connectivity_jni.cpp',
  include_directories: libnativehelper_includes,
  install: true,
)

# android.hardware.SystemSensorManager calls nativeClassInit() when an app
# first asks for SENSOR_SERVICE.  The real JNI is not built (it needs a sensor
# HAL and SensorService), so register host stubs that report no sensors.
shared_library(
  'droidlet_sensors_jni',
  'sensors_jni.cpp',
  include_directories: libnativehelper_includes,
  install: true,
)

# Shared Wayland display context used by the presenter and the EGL bridges.
# It must be defined before libhwui and the presenter library so both can link
# the same instance.
droidlet_wayland = shared_library(
  'droidlet_wayland',
  'wayland_host.cpp',
  include_directories: '.',
  dependencies: [dependency('wayland-client')],
  install: true,
)
droidlet_wayland_dep = declare_dependency(
  include_directories: '.',
  link_with: droidlet_wayland,
)
