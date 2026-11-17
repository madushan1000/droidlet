#!/bin/bash
#
# Build the JniFixture APK.  It contains two bionic-linked libraries:
#   libjnidep.so      - a dependency with a plain C function
#   libjnifixture.so  - JNI entry points, linked against libjnidep,
#                       liblog and libandroid
#
# The native libraries are built from source with the Android runtime sysroot
# under tmp/runtime, so the fixture exercises the same bionic ABI as real NDK
# apps.  Set DROIDLET_CLANGXX to override the compiler.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
DROIDLET_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${1:-${DROIDLET_TEST_APKS:-"$ROOT/droidlet_test_apks"}}
SDK=${DROIDLET_ANDROID_SYSROOT:-"$ROOT/tmp/runtime/mainline/runtime/sdk/android/x86_64"}
CLANGXX=${DROIDLET_CLANGXX:-}
if [ -z "$CLANGXX" ]; then
  CLANGXX=$(command -v clang++-20 || command -v clang++ || true)
fi
if [ -z "$CLANGXX" ]; then
  echo "error: no clang++ found (set DROIDLET_CLANGXX)" >&2
  exit 1
fi
if [ ! -f "$SDK/lib/libc.so" ] || [ ! -f "$SDK/lib/crtbegin_so.o" ]; then
  echo "error: Android runtime sysroot not found at $SDK" >&2
  echo "       set DROIDLET_ANDROID_SYSROOT" >&2
  exit 1
fi

SRC="$DROIDLET_ROOT/testapps/jni-fixture"
BUILD=$(mktemp -d "${TMPDIR:-/tmp/opencode}/droidlet-jni-fixture-XXXXXX")
trap 'rm -rf "$BUILD"' EXIT

COMMON=(
  --target=x86_64-linux-android26
  -fuse-ld=lld
  -fPIC
  -shared
  -nostdlib
  -isystem "$SDK/include/bionic/libc/include"
  -isystem "$SDK/include/bionic/libc/kernel/uapi"
  -isystem "$SDK/include/bionic/libc/kernel/android/uapi"
  -isystem "$ROOT/bionic/libc/kernel/uapi/asm-x86"
  -I "$ROOT/libnativehelper/include_jni"
  -Wl,--hash-style=both
)

# Link-only stubs for the Android liblog/libandroid sonames.  At run time the
# bionic loader resolves those names to droidlet's host libraries; the stubs
# only put the DT_NEEDED entries into libjnifixture.so.
cat > "$BUILD/empty.c" <<'C'
void droidlet_jni_fixture_stub(void) {}
C
cc -shared -fPIC "$BUILD/empty.c" -Wl,-soname,libandroid.so -o "$BUILD/libandroid.so"
cc -shared -fPIC "$BUILD/empty.c" -Wl,-soname,liblog.so -o "$BUILD/liblog.so"

"$CLANGXX" "${COMMON[@]}" -Wl,-soname,libjnidep.so \
  "$SDK/lib/crtbegin_so.o" "$SRC/native/dep.cpp" \
  -L"$SDK/lib" -lc -lm -ldl "$SDK/lib/crtend_so.o" \
  -o "$BUILD/libjnidep.so"

"$CLANGXX" "${COMMON[@]}" -Wl,-soname,libjnifixture.so \
  "$SDK/lib/crtbegin_so.o" "$SRC/native/fixture.cpp" \
  -L"$BUILD" -ljnidep -landroid -llog \
  -L"$SDK/lib" -lc -lm -ldl "$SDK/lib/crtend_so.o" \
  -o "$BUILD/libjnifixture.so"

python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$SRC" --output "$OUT" --name JniFixture \
  --native-lib "$BUILD/libjnidep.so" \
  --native-lib "$BUILD/libjnifixture.so"

echo "built $OUT/JniFixture.apk"
