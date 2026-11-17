#!/bin/bash

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$SCRIPT_DIR" || exit 1

PATH=${PATH}:${SCRIPT_DIR}

export PREFIX=${SCRIPT_DIR}/..
export ANDROID_DATA=${PREFIX}/android-data
export LD_LIBRARY_PATH=${PREFIX}/lib/x86_64-linux-gnu
# Choreographer's vsync path needs the SurfaceFlinger scheduler, which the
# in-process composer does not implement.  Use its timer-based frame
# scheduling instead so that ViewRootImpl runs traversals.
export HOST_PROPERTIES="ro.product.cpu.abilist64=x86_64;ro.dalvik.vm.native.bridge=0;ro.build.version.sdk_full=37.0;debug.choreographer.vsync=false"
export ANDROID_ROOT=${PREFIX}
export ANDROID_ART_ROOT=${PREFIX}
export ANDROID_I18N_ROOT=${PREFIX}
export ANDROID_TZDATA_ROOT=${PREFIX}
export BOOTCLASSPATH=${PREFIX}/framework/framework-overlay-hostdex.jar:${PREFIX}/framework/framework-hostdex.jar:${PREFIX}/framework/core-oj-hostdex.jar:${PREFIX}/framework/core-libart-hostdex.jar:${PREFIX}/framework/icu4j-hostdex.jar:${PREFIX}/framework/okhttp-hostdex.jar:${PREFIX}/framework/apache_xml-hostdex.jar:${PREFIX}/framework/bouncycastle-hostdex.jar:${PREFIX}/framework/conscrypt-hostdex.jar
# --dpi 240 --size 600x1024 \

mkdir -p ${ANDROID_DATA}/dalvik-cache/x86_64

droidlet \
    -verbose:class \
    "$@" \
