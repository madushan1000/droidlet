#!/bin/bash
#
# Build the ExoPlayer smoke-test APK from the AndroidX Media3 artifacts in the
# local Gradle cache.  The test app is a tiny SurfaceView player; the point is
# to exercise MediaCodec/MediaCodecList/AudioTrack with a real media3 build.
#
# Usage: tools/build_exoplayer_apk.sh [output-dir]
#
# The media3 1.11.1 artifacts are expected under ~/.gradle/caches (normally
# populated by the Jetchat/Jetcaster samples).  Override the root with
# GRADLE_CACHE if needed.

set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
DROIDLET_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${1:-${DROIDLET_TEST_APKS:-"$ROOT/droidlet_test_apks"}}
CACHE=${GRADLE_CACHE:-$HOME/.gradle/caches}

find_transformed() {
  find "$CACHE" -path '*transformed*' -name "$1" -print -quit 2>/dev/null
}

find_module() {
  find "$CACHE/modules-2" -name "$1" -print -quit 2>/dev/null
}

args=()
add_jar() {
  local path
  path=$("$1" "$2")
  if [ -z "$path" ]; then
    echo "error: $2 not found under $CACHE" >&2
    exit 1
  fi
  args+=(--dex-classpath "$path")
}

for jar in \
  media3-exoplayer-1.11.1-runtime.jar \
  media3-common-1.11.1-runtime.jar \
  media3-container-1.11.1-runtime.jar \
  media3-datasource-1.11.1-runtime.jar \
  media3-decoder-1.11.1-runtime.jar \
  media3-extractor-1.11.1-runtime.jar \
  media3-database-1.11.1-runtime.jar \
  exifinterface-1.4.2-runtime.jar; do
  add_jar find_transformed "$jar"
done

for jar in \
  guava-33.3.1-android.jar \
  annotation-jvm-1.10.0.jar \
  collection-jvm-1.6.0.jar \
  concurrent-futures-1.2.0.jar \
  failureaccess-1.0.2.jar \
  listenablefuture-9999.0-empty-to-avoid-conflict-with-guava.jar \
  jsr305-3.0.2.jar \
  kotlin-stdlib-1.9.20.jar; do
  add_jar find_module "$jar"
done

python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$DROIDLET_ROOT/testapps/exoplayer-api" \
  --output "$OUT" \
  --name ExoPlayerApi \
  "${args[@]}"
