#!/bin/bash
#
# Build the droidlet test APKs from droidlet-samples.
#
# The selection is deliberately biased towards single-process phone apps that
# use only the framework (no Play services, no HALs):
#
#   legacy/     AOSP samples without Gradle; built with tools/build_legacy_apk.py
#   testapps/   droidlet-owned fixtures (input, lifecycle, ...)
#   devbytes/   pure-framework animation and bitmap samples
#   compose/    self-contained Jetpack Compose apps (Reply, Jetsnack, ...)
#   platform/   the platform-samples catalog app (large, includes many demos)
#
# Output defaults to <workspace>/droidlet_test_apks.  Override with the first
# argument or DROIDLET_TEST_APKS.
#
# Requirements:
#   - Android SDK with a recent platform and build-tools (ANDROID_HOME)
#   - JDK 17+ (JAVA_HOME)
#   - network access for the Gradle-based samples on first run

set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
DROIDLET_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SAMPLES=${DROIDLET_SAMPLES:-"$ROOT/droidlet-samples"}
OUT=${1:-${DROIDLET_TEST_APKS:-"$ROOT/droidlet_test_apks"}}
export ANDROID_HOME=${ANDROID_HOME:-$HOME/tools/android-sdk}
export ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT:-$ANDROID_HOME}
if [ -z "${JAVA_HOME:-}" ] && [ -d /usr/lib/jvm/java-21-openjdk-amd64 ]; then
  export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
fi
export PATH="$ANDROID_HOME/platform-tools:$PATH"

if [ ! -d "$SAMPLES" ]; then
  echo "error: droidlet-samples not found at $SAMPLES" >&2
  echo "       set DROIDLET_SAMPLES=/path/to/droidlet-samples" >&2
  exit 1
fi
mkdir -p "$OUT"

legacy() {
  local sample=$1
  local name=$2
  shift 2
  echo "== legacy: $name ($sample)"
  python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
    --sample "$SAMPLES/$sample" --output "$OUT" --name "$name" "$@" \
    || echo "warning: failed to build $name" >&2
}

gradle_app() {
  local project=$1
  local name=$2
  echo "== gradle: $name ($project)"
  if ! ( cd "$SAMPLES/$project" &&
         ./gradlew :app:assembleDebug --no-daemon --console=plain ); then
    echo "warning: failed to build $name" >&2
    return
  fi
  cp "$SAMPLES/$project/app/build/outputs/apk/debug/app-debug.apk" \
     "$OUT/$name.apk"
  echo "   -> $OUT/$name.apk"
}

# --- pure-framework AOSP samples -------------------------------------------
legacy development/samples/HelloActivity                 HelloActivity
legacy development/samples/SkeletonApp                   SkeletonApp
legacy development/samples/FixedGridLayout               FixedGridLayout
legacy development/samples/Spinner                       Spinner
legacy development/samples/CrossCompatibility            TouchExample
legacy development/samples/Snake                         Snake
legacy development/samples/LunarLander                   LunarLander
legacy development/samples/HeavyWeight                   HeavyWeight
legacy development/samples/NotePad                       NotePad
legacy development/samples/TicTacToeMain                 TicTacToe \
  --extra-src "$SAMPLES/development/samples/TicTacToeLib/src" \
  --extra-res "$SAMPLES/development/samples/TicTacToeLib/res" \
  --extra-package com.example.android.tictactoe.library
legacy development/samples/training/basic/ActivityLifecycle ActivityLifecycle
legacy development/samples/browseable/PdfRendererBasic    PdfRendererBasic \
  --assets "$SAMPLES/platform-samples/samples/graphics/pdf/src/main/assets"
legacy development/apps/NinePatchLab                      NinePatchLab

# --- droidlet-owned fixtures ------------------------------------------------
echo "== testapp: ClickCounter"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/click-counter" --output "$OUT" \
  --name ClickCounter \
  || echo "warning: failed to build ClickCounter" >&2

echo "== testapp: ActivityStack"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/activity-stack" --output "$OUT" \
  --name ActivityStack \
  || echo "warning: failed to build ActivityStack" >&2

echo "== testapp: ProviderApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/provider-app" --output "$OUT" \
  --name ProviderApp \
  || echo "warning: failed to build ProviderApp" >&2

echo "== testapp: DialogApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/dialog-app" --output "$OUT" \
  --name DialogApp \
  || echo "warning: failed to build DialogApp" >&2

echo "== testapp: SurfaceClip"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/surface-clip" --output "$OUT" \
  --name SurfaceClip \
  || echo "warning: failed to build SurfaceClip" >&2

echo "== testapp: AnimationApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/animation-app" --output "$OUT" \
  --name AnimationApp \
  || echo "warning: failed to build AnimationApp" >&2

echo "== testapp: MenuApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/menu-app" --output "$OUT" \
  --name MenuApp \
  || echo "warning: failed to build MenuApp" >&2

echo "== testapp: PopupApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/popup-app" --output "$OUT" \
  --name PopupApp \
  || echo "warning: failed to build PopupApp" >&2

echo "== testapp: ServiceApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/service-app" --output "$OUT" \
  --name ServiceApp \
  || echo "warning: failed to build ServiceApp" >&2

echo "== testapp: ServiceProcessApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/service-process-app" --output "$OUT" \
  --name ServiceProcessApp \
  || echo "warning: failed to build ServiceProcessApp" >&2

echo "== testapp: ServiceStartedApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/service-started-app" --output "$OUT" \
  --name ServiceStartedApp \
  || echo "warning: failed to build ServiceStartedApp" >&2

echo "== testapp: ServiceFdApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/service-fd-app" --output "$OUT" \
  --name ServiceFdApp \
  || echo "warning: failed to build ServiceFdApp" >&2

echo "== testapp: BroadcastApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/broadcast-app" --output "$OUT" \
  --name BroadcastApp \
  || echo "warning: failed to build BroadcastApp" >&2

echo "== testapp: AlarmJobApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/alarm-job-app" --output "$OUT" \
  --name AlarmJobApp \
  || echo "warning: failed to build AlarmJobApp" >&2

# SensorManager initializes through JNI; the host stub must report an empty
# sensor list rather than an UnsatisfiedLinkError.
echo "== testapp: SensorApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/sensor-app" --output "$OUT" \
  --name SensorApp \
  || echo "warning: failed to build SensorApp" >&2

# PowerManager's SystemServiceRegistry fetcher resolves both "power" and
# "thermalservice" before it constructs the manager; a missing one returns
# null from getSystemService().  Glmark2Activity takes a wake lock in onCreate.
echo "== testapp: PowerApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/power-app" --output "$OUT" \
  --name PowerApp \
  || echo "warning: failed to build PowerApp" >&2

# ConnectivityManager callbacks: unregisterNetworkCallback() must find the
# NetworkRequest the service returned or it throws
# "NetworkCallback was not registered" (Glide's monitor does this in onStop).
echo "== testapp: NetworkApp"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/network-app" --output "$OUT" \
  --name NetworkApp \
  || echo "warning: failed to build NetworkApp" >&2

echo "== testapp: JniFixture"
"$DROIDLET_ROOT/tools/build_jni_fixture.sh" "$OUT" \
  || echo "warning: failed to build JniFixture" >&2

echo "== testapp: VulkanFixture"
"$DROIDLET_ROOT/tools/build_vulkan_fixture.sh" "$OUT" \
  || echo "warning: failed to build VulkanFixture" >&2

echo "== testapp: MediaApi"
python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/media-api" --output "$OUT" \
  --name MediaApi \
  || echo "warning: failed to build MediaApi" >&2

echo "== testapp: ExoPlayerApi"
if [ -d "${GRADLE_CACHE:-$HOME/.gradle/caches}" ]; then
  "$DROIDLET_ROOT/tools/build_exoplayer_apk.sh" "$OUT" \
    || echo "warning: failed to build ExoPlayerApi" >&2
else
  echo "skipping ExoPlayerApi: no Gradle cache with media3 artifacts" >&2
fi

if [ "${DROIDLET_TEST_APKS_MINIMAL:-0}" != "1" ]; then
# --- devbytes: animation / bitmap / layout stress ---------------------------
for sample in \
  graphics/BitmapScaling \
  graphics/ImagePixelization \
  graphics/BitmapAllocation \
  animation/Bouncer \
  animation/CrossFading \
  animation/CurvedMotion \
  animation/KeyframeAnimation \
  animation/ViewAnimations \
  animation/PropertyAnimations \
  animation/MultiPropertyAnimations \
  animation/CardFlip \
  animation/ListViewItemAnimations \
  ui/RequestDuringLayout ; do
  legacy "development/samples/devbytes/$sample" "$(basename "$sample")"
done

# --- AndroidX platform fixtures --------------------------------------------
# droidlet-owned Gradle fixture exercising AppCompatActivity, AndroidX
# Startup, WorkManager, FileProvider and the platform service shims.  It lives
# under testapps/, not droidlet-samples.
echo "== testapp: AndroidXApp"
if ! ( cd "$DROIDLET_ROOT/testapps/androidx-app" &&
       ./gradlew :app:assembleDebug --no-daemon --console=plain ); then
  echo "warning: failed to build AndroidXApp" >&2
else
  cp "$DROIDLET_ROOT/testapps/androidx-app/app/build/outputs/apk/debug/app-debug.apk" \
     "$OUT/AndroidXApp.apk"
  echo "   -> $OUT/AndroidXApp.apk"
fi

# --- Compose apps -----------------------------------------------------------
gradle_app compose-samples/Reply    ComposeReply
gradle_app compose-samples/Jetsnack ComposeJetsnack
gradle_app compose-samples/JetNews  ComposeJetNews
gradle_app compose-samples/Jetchat  ComposeJetchat

# --- platform-samples catalog ----------------------------------------------
gradle_app platform-samples PlatformSamples
fi  # DROIDLET_TEST_APKS_MINIMAL

echo
echo "APKs in $OUT:"
ls -lh "$OUT"/*.apk 2>/dev/null || echo "  (none)"
