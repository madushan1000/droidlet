#!/bin/bash
#
# Build the VulkanFixture APK.  The native library is bionic-linked and
# creates a VK_KHR_android_surface from the Java Surface's ANativeWindow.
#
# The Android runtime sysroot has no Vulkan headers, so the host's Khronos
# headers are used for declarations only; all entry points are resolved with
# dlopen/dlsym at run time.

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
  exit 1
fi
if [ ! -f /usr/include/vulkan/vulkan.h ]; then
  echo "error: host Vulkan headers not found (install libvulkan-dev)" >&2
  exit 1
fi

SRC="$DROIDLET_ROOT/testapps/vulkan-fixture"
BUILD=$(mktemp -d "${TMPDIR:-/tmp/opencode}/droidlet-vulkan-fixture-XXXXXX")
trap 'rm -rf "$BUILD"' EXIT
mkdir -p "$BUILD/include"
ln -s /usr/include/vulkan "$BUILD/include/vulkan"

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
  -I "$BUILD/include"
  -I "$ROOT/frameworks/native/include"
  -I "$ROOT/frameworks/native/libs/nativewindow/include"
  -I "$ROOT/frameworks/native/libs/arect/include"
  -I "$ROOT/libnativehelper/include_jni"
  -Wl,--hash-style=both
)

cat > "$BUILD/empty.c" <<'C'
void droidlet_vulkan_fixture_stub(void) {}
C
cc -shared -fPIC "$BUILD/empty.c" -Wl,-soname,libandroid.so -o "$BUILD/libandroid.so"
cc -shared -fPIC "$BUILD/empty.c" -Wl,-soname,liblog.so -o "$BUILD/liblog.so"

"$CLANGXX" "${COMMON[@]}" -Wl,-soname,libvulkanfixture.so \
  "$SDK/lib/crtbegin_so.o" "$SRC/native/fixture.cpp" \
  -L"$BUILD" -landroid -llog \
  -L"$SDK/lib" -lc -lm -ldl "$SDK/lib/crtend_so.o" \
  -o "$BUILD/libvulkanfixture.so"

python3 "$DROIDLET_ROOT/tools/build_legacy_apk.py" \
  --sample "$SRC" --output "$OUT" --name VulkanFixture \
  --native-lib "$BUILD/libvulkanfixture.so"

echo "built $OUT/VulkanFixture.apk"
