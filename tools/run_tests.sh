#!/bin/bash
#
# Single CI entry point for the host-side regression suite.
#
#   tools/run_tests.sh                 # build minimal fixtures, then test
#   tools/run_tests.sh --skip-build    # test the existing APK set
#
# Covers:
#   - golden-image fixtures (including dialogs, popups and checkable menus)
#   - bounded --exit-after-script input runs
#   - logcat-style ANDROID_LOG_TAGS / DROIDLET_LOG_TAGS filtering
#   - fd-leak accounting with DROIDLET_FAIL_ON_FD_LEAK
#   - bundled JNI: bionic libraries, bundled dependencies, liblog/libandroid

set -uo pipefail

DROIDLET_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORKSPACE=$(cd "$DROIDLET_ROOT/.." && pwd)
APK_DIR=${DROIDLET_TEST_APKS:-"$WORKSPACE/droidlet_test_apks"}
RUN_APK="$WORKSPACE/run_apk.sh"
BUILD_DIR=${DROIDLET_BUILD_DIR:-"$WORKSPACE/builddir"}

SKIP_BUILD=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    -h|--help)
      sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "error: unknown option $arg" >&2
      exit 2
      ;;
  esac
done

export DROIDLET_TEST_APKS="$APK_DIR"
export DROIDLET_PRESENT=null
unset DROIDLET_EXIT_AFTER_RESUME DROIDLET_INPUT_EVENTS DROIDLET_INPUT_SCRIPT
unset DROIDLET_LOG_TAGS ANDROID_LOG_TAGS DROIDLET_LOG_LEVEL

TMP=$(mktemp -d "${TMPDIR:-/tmp/opencode}/droidlet-tests-XXXXXX")
TEST_WESTON_PID=
TEST_XVFB_PID=

kill_test_group() {
  local pid=$1
  [ -n "$pid" ] || return 0
  kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
  for _ in $(seq 1 20); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.05
  done
  kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

cleanup() {
  kill_test_group "$TEST_WESTON_PID"
  kill_test_group "$TEST_XVFB_PID"
  TEST_WESTON_PID=
  TEST_XVFB_PID=
  if [ "${DROIDLET_KEEP_TEST_LOGS:-0}" = 1 ]; then
    echo "test logs kept in $TMP" >&2
  else
    rm -rf "$TMP"
  fi
}
trap cleanup EXIT INT TERM

FAILED=0
fail() {
  echo "FAIL: $*" >&2
  FAILED=1
}

# hwui always uses a real EGL display now.  It prefers X11 (stable with
# multiple offscreen surfaces), so start a headless X server when the caller
# did not provide one.  Native NDK EGL additionally uses Wayland; start a
# headless Weston when possible.
if [ -z "${DISPLAY:-}" ]; then
  if ! command -v Xvfb >/dev/null 2>&1; then
    fail "no DISPLAY and Xvfb is not installed"
  else
    for _display in $(seq 99 110); do
      setsid Xvfb ":$_display" -screen 0 1280x800x24 >"$TMP/xvfb.log" 2>&1 &
      _pid=$!
      _ready=0
      for _try in $(seq 1 50); do
        if command -v xdpyinfo >/dev/null 2>&1; then
          xdpyinfo -display ":$_display" >/dev/null 2>&1 && _ready=1 && break
        elif kill -0 "$_pid" 2>/dev/null; then
          _ready=1
          break
        fi
        kill -0 "$_pid" 2>/dev/null || break
        sleep 0.1
      done
      if [ "$_ready" = 1 ]; then
        export DISPLAY=":$_display"
        TEST_XVFB_PID=$_pid
        break
      fi
      kill "$_pid" 2>/dev/null || true
    done
    if [ -z "$TEST_XVFB_PID" ]; then
      fail "could not start Xvfb"
    fi
  fi
  unset _display _pid 2>/dev/null || true
fi

if [ -z "${WAYLAND_DISPLAY:-}" ] && command -v weston >/dev/null 2>&1; then
  export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-"$TMP/xdg"}
  mkdir -p "$XDG_RUNTIME_DIR"
  chmod 700 "$XDG_RUNTIME_DIR"
  export WAYLAND_DISPLAY="droidlet-test-$$"
  setsid weston --backend=headless-backend.so --socket="$WAYLAND_DISPLAY" \
    --width=1280 --height=800 >"$TMP/weston.log" 2>&1 &
  TEST_WESTON_PID=$!
  for _ in $(seq 1 50); do
    [ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ] && break
    sleep 0.1
  done
  if [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; then
    fail "weston did not start; see $TMP/weston.log"
  fi
fi

run_bounded() {
  local name=$1
  shift
  local log="$TMP/$name.log"
  echo "== $name"
  if timeout -k 5 45 "$RUN_APK" "$@" >"$log" 2>&1; then
    echo "   ok"
  else
    local status=$?
    echo "   failed (exit $status); log: $log" >&2
    tail -25 "$log" | sed 's/^/   | /' >&2
    fail "$name"
  fi
  RUN_LOG="$log"
}

if [ "$SKIP_BUILD" -eq 0 ]; then
  echo "== build minimal fixtures"
  if ! DROIDLET_TEST_APKS_MINIMAL=1 \
      "$DROIDLET_ROOT/tools/build_test_apks.sh" "$APK_DIR" >"$TMP/build.log" 2>&1; then
    echo "   build failed; log: $TMP/build.log" >&2
    tail -30 "$TMP/build.log" | sed 's/^/   | /' >&2
    fail "fixture build"
  else
    echo "   ok"
  fi
fi

for apk in HelloActivity SkeletonApp FixedGridLayout ClickCounter NotePad \
           DialogApp PopupApp MenuApp JniFixture VulkanFixture NetworkApp; do
  if [ ! -f "$APK_DIR/$apk.apk" ]; then
    fail "missing $APK_DIR/$apk.apk (run without --skip-build)"
  fi
done

echo "== golden images"
if "$DROIDLET_ROOT/tools/golden_tests.py"; then
  echo "   ok"
else
  fail "golden images"
fi

# --input-script + --exit-after-script must terminate a bounded run itself.
printf '300 tap 640 60\n' >"$TMP/input.txt"
DROIDLET_APK="$APK_DIR/ClickCounter.apk" \
  run_bounded bounded-input --input-script "$TMP/input.txt" --exit-after-script
if ! grep -q "exit monitor: reached target" "$RUN_LOG"; then
  fail "bounded-input did not reach the exit target"
fi

# A tooltip appears when the pointer rests on the overflow button.  Its
# non-touchable window must not become the input target, or the menu opened
# afterwards receives no clicks and looks frozen (glmark2's overflow menu).
echo "== menu tooltip"
if [ ! -f "$APK_DIR/MenuApp.apk" ]; then
  fail "missing $APK_DIR/MenuApp.apk (run without --skip-build)"
else
  DROIDLET_INPUT_EVENTS="1000 hover 1252 26;2200 tap 1252 26;3200 tap 1180 33;4400 hover 1252 26;5600 tap 1252 26;7000 tap 1200 81;7800 hover 640 400" \
    DROIDLET_APK="$APK_DIR/MenuApp.apk" \
    run_bounded menu-tooltip --no-screenshot --exit-after-script
  for expected in \
      "DroidletMenu: menu lines=true coordinates=false rings=false" \
      "DroidletMenu: menu lines=true coordinates=true rings=false"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "menu tooltip: missing \"$expected\""
    fi
  done
fi
if pgrep -x droidlet >/dev/null; then
  fail "menu tooltip: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# App-defined services: startService + bindService + binder call + unbind +
# stopService, all in the app's process.  The fixture must destroy the service
# and let the host exit on its own.
echo "== app services"
if [ ! -f "$APK_DIR/ServiceApp.apk" ]; then
  fail "missing $APK_DIR/ServiceApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/ServiceApp.apk" \
    run_bounded app-services --no-screenshot
  for expected in \
      "ServiceApp: service onCreate" \
      "ServiceApp: service onStartCommand id=1" \
      "ServiceApp: onServiceConnected" \
      "ServiceApp: add=5" \
      "ServiceApp: service onUnbind" \
      "ServiceApp: service onDestroy" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app services: missing \"$expected\""
    fi
  done
  if grep -qE "bindService returned false|binder did not arrive" "$RUN_LOG"; then
    fail "app services: binder did not reach the client"
  fi
fi

# Same-app android:process service over binder RPC on a nameless Unix socket
# pair.  The child host must exit with the parent; kill any survivor.
echo "== app service process"
if [ ! -f "$APK_DIR/ServiceProcessApp.apk" ]; then
  fail "missing $APK_DIR/ServiceProcessApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/ServiceProcessApp.apk" \
    run_bounded app-service-process --no-screenshot
  for expected in \
      "ServiceProcessApp: onServiceConnected" \
      "ServiceProcessApp: remote service onBind" \
      "ServiceProcessApp: transact=true add=5" \
      "ServiceProcessApp: remote service onUnbind" \
      "ServiceProcessApp: remote service onDestroy" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app service process: missing \"$expected\""
    fi
  done
  if grep -qE "bindService returned false|host unavailable|DeadObjectException" "$RUN_LOG"; then
    fail "app service process: binder RPC failed"
  fi
fi
if pgrep -x droidlet >/dev/null; then
  fail "app service process: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# Started services in a child process: onCreate/onStartCommand, parent
# stopService, and stopSelf with the parent noticing the host exit.
echo "== app started service process"
if [ ! -f "$APK_DIR/ServiceStartedApp.apk" ]; then
  fail "missing $APK_DIR/ServiceStartedApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/ServiceStartedApp.apk" \
    run_bounded app-started-service --no-screenshot
  for expected in \
      "ServiceStartedApp: startService returned" \
      "ServiceStartedApp: started service onStartCommand id=1" \
      "ServiceStartedApp: self service onStartCommand id=2" \
      "ServiceStartedApp: activity finished; service remains" \
      "ServiceStartedApp: self service stopSelf id=2" \
      "ServiceStartedApp: self service onDestroy" \
      "DroidletLauncher: service: host exited" \
      "ServiceStartedApp: stopping service" \
      "ServiceStartedApp: started service onDestroy" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app started service: missing \"$expected\""
    fi
  done
fi
if pgrep -x droidlet >/dev/null; then
  fail "app started service: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# File descriptors over the same RPC transport (ParcelFileDescriptor).
echo "== app service fd transfer"
if [ ! -f "$APK_DIR/ServiceFdApp.apk" ]; then
  fail "missing $APK_DIR/ServiceFdApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/ServiceFdApp.apk" \
    run_bounded app-service-fd --no-screenshot
  for expected in \
      "ServiceFdApp: onServiceConnected" \
      "ServiceFdApp: service received=hello" \
      "ServiceFdApp: service callback=99" \
      "ServiceFdApp: transact=true fd reply=echo:hello" \
      "ServiceFdApp: fd service onDestroy" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app service fd: missing \"$expected\""
    fi
  done
  if grep -qE "fd transaction failed|Attempt to read file descriptor" "$RUN_LOG"; then
    fail "app service fd: file descriptor transfer failed"
  fi
fi
if pgrep -x droidlet >/dev/null; then
  fail "app service fd: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# In-app broadcasts: dynamic and manifest receivers, normal and ordered.
echo "== app broadcasts"
if [ ! -f "$APK_DIR/BroadcastApp.apk" ]; then
  fail "missing $APK_DIR/BroadcastApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/BroadcastApp.apk" \
    run_bounded app-broadcasts --no-screenshot
  for expected in \
      "BroadcastApp: dynamic ping=hello" \
      "BroadcastApp: manifest ping=hello" \
      "BroadcastApp: ordered receiver" \
      "BroadcastApp: ordered result=7 seven" \
      "BroadcastApp: done" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app broadcasts: missing \"$expected\""
    fi
  done
fi
if pgrep -x droidlet >/dev/null; then
  fail "app broadcasts: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# Alarms and jobs continue after the activity is gone.  The delayed job and
# alarm both keep the host process alive.
echo "== app alarms and jobs"
if [ ! -f "$APK_DIR/AlarmJobApp.apk" ]; then
  fail "missing $APK_DIR/AlarmJobApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/AlarmJobApp.apk" \
    run_bounded app-alarms-jobs --no-screenshot
  for expected in \
      "AlarmJobApp: alarm scheduled" \
      "AlarmJobApp: job scheduled result=0" \
      "AlarmJobApp: job started id=1" \
      "AlarmJobApp: job finished id=1" \
      "AlarmJobApp: activity finished; alarm remains" \
      "AlarmJobApp: job started id=2" \
      "AlarmJobApp: job finished id=2" \
      "AlarmJobApp: alarm fired" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app alarms/jobs: missing \"$expected\""
    fi
  done
fi
if pgrep -x droidlet >/dev/null; then
  fail "app alarms/jobs: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# SensorManager initializes its native layer on first use.  The host has no
# sensor HAL, so the JNI stubs must register and report an empty sensor list.
echo "== app sensors"
if [ ! -f "$APK_DIR/SensorApp.apk" ]; then
  fail "missing $APK_DIR/SensorApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/SensorApp.apk" \
    run_bounded app-sensors --no-screenshot
  for expected in \
      "SensorApp: SensorManager created" \
      "SensorApp: sensor count=10" \
      "SensorApp: accelerometer={Sensor name=\"droidlet Accelerometer\"" \
      "SensorApp: registerListener=true" \
      "SensorApp: event x=0.0 y=0.0 z=9.81" \
      "SensorApp: events=3" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app sensors: missing \"$expected\""
    fi
  done
fi
if pgrep -x droidlet >/dev/null; then
  fail "app sensors: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# PowerManager needs both the "power" and "thermalservice" binder services;
# Glmark2Activity takes a screen wake lock as soon as it is created.
echo "== app power"
if [ ! -f "$APK_DIR/PowerApp.apk" ]; then
  fail "missing $APK_DIR/PowerApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/PowerApp.apk" \
    run_bounded app-power --no-screenshot
  for expected in \
      "PowerApp: PowerManager created=true" \
      "PowerApp: interactive=true wakeLockLevelSupported=true" \
      "PowerApp: wakelock held=true" \
      "PowerApp: wakelock released held=false" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app power: missing \"$expected\""
    fi
  done
fi
if pgrep -x droidlet >/dev/null; then
  fail "app power: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# ConnectivityManager keeps the NetworkRequest returned by requestNetwork()/
# listenForNetwork() on the NetworkCallback; unregisterNetworkCallback() throws
# IllegalArgumentException("NetworkCallback was not registered") when it is
# null.  Glide unregisters its default callback from onStop.
echo "== app network callbacks"
if [ ! -f "$APK_DIR/NetworkApp.apk" ]; then
  fail "missing $APK_DIR/NetworkApp.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/NetworkApp.apk" \
    run_bounded app-network --no-screenshot
  for expected in \
      "NetworkApp: ConnectivityManager created=true" \
      "NetworkApp: default callback registered" \
      "NetworkApp: callback request registered" \
      "NetworkApp: callbacks unregistered" \
      "task stack empty; exiting"; do
    if ! grep -qF "$expected" "$RUN_LOG"; then
      fail "app network callbacks: missing \"$expected\""
    fi
  done
  if grep -q "NetworkCallback was not registered" "$RUN_LOG"; then
    fail "app network callbacks: unregister lost the NetworkRequest"
  fi
fi
if pgrep -x droidlet >/dev/null; then
  fail "app network callbacks: leftover droidlet process"
  pkill -9 -x droidlet || true
fi

# Android's standard liblog filter syntax, with tags and priorities.
if ! DROIDLET_LOG_TAGS="HostComposer:V *:S" \
    DROIDLET_APK="$APK_DIR/HelloActivity.apk" \
    timeout -k 5 40 "$RUN_APK" --exit-after-frame 2 >"$TMP/filter.log" 2>&1; then
  fail "liblog tag filter run"
else
  if ! grep -q "HostComposer:" "$TMP/filter.log"; then
    fail "HostComposer logs missing under DROIDLET_LOG_TAGS"
  fi
  if grep -q "DroidletLauncher:" "$TMP/filter.log"; then
    fail "DroidletLauncher logs leaked through DROIDLET_LOG_TAGS"
  fi
  echo "== log filtering (DROIDLET_LOG_TAGS)"
  echo "   ok"
fi

# The same expression through the standard ANDROID_LOG_TAGS variable.
if ANDROID_LOG_TAGS="DroidletLauncher:V *:S" \
    DROIDLET_APK="$APK_DIR/HelloActivity.apk" \
    timeout -k 5 40 "$RUN_APK" --exit-after-frame 2 >"$TMP/filter-android.log" 2>&1; then
  if ! grep -q "DroidletLauncher:" "$TMP/filter-android.log"; then
    fail "DroidletLauncher logs missing under ANDROID_LOG_TAGS"
  fi
  if grep -q "HostComposer:" "$TMP/filter-android.log"; then
    fail "HostComposer logs leaked through ANDROID_LOG_TAGS"
  fi
  echo "== log filtering (ANDROID_LOG_TAGS)"
  echo "   ok"
else
  fail "ANDROID_LOG_TAGS filter run"
fi

# Shutdown fd accounting; the exit hook fails the process if the allowance is
# exceeded.
if DROIDLET_FAIL_ON_FD_LEAK=1 DROIDLET_FD_LEAK_ALLOWANCE=64 \
    DROIDLET_APK="$APK_DIR/HelloActivity.apk" \
    timeout -k 5 40 "$RUN_APK" --exit-after-frame 2 >"$TMP/fd.log" 2>&1; then
  if ! grep -q "exit: open fds=" "$TMP/fd.log"; then
    fail "fd accounting log missing"
  fi
  echo "== fd accounting"
  echo "   ok"
else
  fail "fd accounting (DROIDLET_FAIL_ON_FD_LEAK)"
fi

# Bundled JNI: bionic-linked library, bundled-to-bundled dependency, and
# liblog/libandroid resolution through droidlet's host libraries.
echo "== jni fixture"
if [ ! -f "$APK_DIR/JniFixture.apk" ]; then
  fail "missing $APK_DIR/JniFixture.apk (run without --skip-build)"
else
  DROIDLET_APK="$APK_DIR/JniFixture.apk" DROIDLET_EXIT_AFTER_RESUME=1 \
    run_bounded jni-fixture --no-screenshot
  if ! grep -q "JNI ADD=42" "$RUN_LOG"; then
    fail "jni fixture did not return the native add result"
  fi
  if ! grep -q "ASSET_MANAGER=true" "$RUN_LOG"; then
    fail "jni fixture did not resolve libandroid"
  fi
  if ! grep -q "SENSOR_MANAGER=true" "$RUN_LOG"; then
    fail "jni fixture did not resolve the sensor NDK stub"
  fi
  if ! grep -q "EGL=true" "$RUN_LOG"; then
    fail "jni fixture did not initialize host EGL"
  fi
  if ! grep -q "DIR=.*/lib/x86_64" "$RUN_LOG"; then
    fail "jni fixture nativeLibraryDir missing"
  fi
  for lib in libjnifixture.so libjnidep.so; do
    if [ ! -f "$WORKSPACE/android-data/app/com.example.droidlet.jnifixture/lib/x86_64/$lib" ]; then
      fail "jni fixture did not extract $lib"
    fi
  done
  if grep -q "bionic JNI load failed" "$RUN_LOG"; then
    fail "jni fixture hit a bionic loader error"
  fi
fi

# Vulkan: bionic app creates a VK_KHR_android_surface and presents a cleared
# swapchain; the host bridge copies the image into the Android window.
echo "== vulkan fixture"
if [ ! -f "$APK_DIR/VulkanFixture.apk" ]; then
  fail "missing $APK_DIR/VulkanFixture.apk (run without --skip-build)"
else
  DROIDLET_PRESENT=null \
    DROIDLET_APK="$APK_DIR/VulkanFixture.apk" DROIDLET_EXIT_AFTER_RESUME=1 \
    run_bounded vulkan-fixture --screenshot "$TMP/vulkan.png"
  if ! grep -q "VULKAN_RENDER=0" "$RUN_LOG"; then
    fail "vulkan fixture did not complete rendering"
  else
    read -r red green blue < <(convert "$TMP/vulkan.png" -format \
      '%[fx:int(255*r)] %[fx:int(255*g)] %[fx:int(255*b)]' info:)
    if [ "${red:-0}" -gt 200 ] && [ "${green:-0}" -gt 40 ] \
        && [ "${green:-0}" -lt 170 ] && [ "${blue:-0}" -lt 100 ]; then
      echo "   ok"
    else
      fail "vulkan fixture presented an unexpected centre pixel: ${red:-?},${green:-?},${blue:-?}"
    fi
  fi
fi

# OpenSL ES playback through the host libOpenSLES/libwilhelm.
echo "== opensles smoke"
if [ ! -x "$BUILD_DIR/droidlet/tests/opensles_smoke" ]; then
  fail "missing $BUILD_DIR/droidlet/tests/opensles_smoke (build the project first)"
elif SDL_AUDIODRIVER=dummy timeout 30 "$BUILD_DIR/droidlet/tests/opensles_smoke" \
    >"$TMP/opensles.log" 2>&1; then
  if ! grep -q "callbacks=10" "$TMP/opensles.log"; then
    fail "opensles smoke did not drain all buffers"
  else
    echo "   ok"
  fi
else
  fail "opensles smoke"
  tail -10 "$TMP/opensles.log" | sed 's/^/   | /' >&2
fi

# NativeActivity metadata: the external atl fixture is optional because it is
# not part of droidlet_test_apks.
NATIVE_ACTIVITY_APK="$WORKSPACE/atl_test_apks/ndk_sample_endless_tunnel.apk"
if [ -f "$NATIVE_ACTIVITY_APK" ]; then
  echo "== native activity metadata"
  native_log="$TMP/native-activity.log"
  DROIDLET_APK="$NATIVE_ACTIVITY_APK" DROIDLET_EXIT_AFTER_RESUME=1 \
    timeout -k 5 45 "$RUN_APK" --no-screenshot >"$native_log" 2>&1 || true
  if ! grep -q "lib_name=game" "$native_log"; then
    fail "NativeActivity did not read android.app.lib_name"
  elif ! grep -q "lib/x86_64/libgame.so" "$native_log"; then
    fail "NativeActivity did not load libgame.so"
  elif grep -q "DISABLING SOUND" "$native_log"; then
    fail "Endless Tunnel could not create its OpenSL ES audio player"
  elif grep -q "slCreateEngine" "$native_log"; then
    fail "Endless Tunnel failed to resolve slCreateEngine"
  else
    echo "   ok"
  fi
fi

if [ "$FAILED" -ne 0 ]; then
  echo
  echo "Regression suite FAILED; temporary logs are under $TMP" >&2
  exit 1
fi

echo
echo "Regression suite passed."
