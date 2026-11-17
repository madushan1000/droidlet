/*
 * JNI bridge between android.app.DroidletLauncher and the host composer.
 *
 * Presentation is owned by the native launcher: the composer consumes the real
 * Android Surface/BufferQueue frames and hands them to the Wayland/X11/null
 * presenter.  The Java side only needs the composed frame for --screenshot.
 */

#include <jni.h>

#include <android_view_KeyCharacterMap.h>
#include <binder/IInterface.h>
#include <gui/BLASTBufferQueue.h>
#include <gui/Surface.h>
#include <input/KeyCharacterMap.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "host_composer.h"
#include "input_injector.h"

extern "C" {

// libandroid_runtime only registers this Surface method when built for an
// Android target.  SurfaceView still needs it to attach the BLAST queue's
// producer surface on the host, so provide the same implementation here.  It
// is resolved by symbol name because it was not part of RegisterNatives.
JNIEXPORT jlong JNICALL
Java_android_view_Surface_nativeGetFromBlastBufferQueue(
        JNIEnv*, jclass, jlong nativeObject, jlong blastBufferQueueNativeObj) {
    using namespace android;
    static const void* sRefBaseOwner = nullptr;

    Surface* self = reinterpret_cast<Surface*>(nativeObject);
    BLASTBufferQueue* rawQueue = reinterpret_cast<BLASTBufferQueue*>(blastBufferQueueNativeObj);
    if (rawQueue == nullptr) {
        return nativeObject;
    }
    sp<BLASTBufferQueue> queue = rawQueue;
    const sp<IGraphicBufferProducer>& bufferProducer = queue->getIGraphicBufferProducer();
    if (self != nullptr &&
        IInterface::asBinder(self->getIGraphicBufferProducer()) ==
                IInterface::asBinder(bufferProducer)) {
        return nativeObject;
    }

    sp<Surface> surface = queue->getSurface(true /* includeSurfaceControlHandle */);
    if (surface != nullptr) {
        surface->incStrong(&sRefBaseOwner);
    }
    return reinterpret_cast<jlong>(surface.get());
}

// Creates the app side of a fresh InputChannel pair.  The launcher copies this
// channel into the out parameter of IWindowSession.addToDisplayAsUser(); the
// server side stays in input_injector.cpp.
JNIEXPORT jobject JNICALL
Java_android_app_DroidletLauncher_nativeCreateClientInputChannel(JNIEnv* env, jclass) {
    return droidlet::createClientInputChannel(env);
}

// Loads a real Android key character map (the host tree installs Virtual.kcm)
// so key events produce Unicode text instead of hitting an empty map.
JNIEXPORT jobject JNICALL
Java_android_app_DroidletLauncher_nativeLoadKeyCharacterMap(JNIEnv* env, jclass,
                                                            jstring path) {
    if (path == nullptr) {
        return nullptr;
    }
    const char* chars = env->GetStringUTFChars(path, nullptr);
    if (chars == nullptr) {
        return nullptr;
    }
    auto result = android::KeyCharacterMap::load(chars,
                                                 android::KeyCharacterMap::Format::BASE);
    env->ReleaseStringUTFChars(path, chars);
    if (!result.ok()) {
        return nullptr;
    }
    return android::android_view_KeyCharacterMap_create(env, 0, std::move(*result));
}

JNIEXPORT jint JNICALL
Java_android_app_DroidletLauncher_nativeLastInputChannelId(JNIEnv*, jclass) {
    return droidlet::lastInputChannelId();
}

JNIEXPORT void JNICALL
Java_android_app_DroidletLauncher_nativeSetActiveInputChannel(JNIEnv*, jclass, jint id) {
    droidlet::setActiveInputChannel(id);
}

JNIEXPORT void JNICALL
Java_android_app_DroidletLauncher_nativeSetInputWindowOffset(JNIEnv*, jclass, jfloat x,
                                                             jfloat y) {
    droidlet::setInputWindowOffset(x, y);
}

JNIEXPORT void JNICALL
Java_android_app_DroidletLauncher_nativeSetInputWindowInset(JNIEnv*, jclass, jfloat x,
                                                            jfloat y) {
    droidlet::setInputWindowInset(x, y);
}

JNIEXPORT void JNICALL
Java_android_app_DroidletLauncher_nativeSetActiveInputLayer(JNIEnv*, jclass, jint layerId) {
    droidlet::setActiveInputLayer(layerId);
}

JNIEXPORT void JNICALL
Java_android_app_DroidletLauncher_nativeSetInputFocus(JNIEnv*, jclass, jboolean focused) {
    droidlet::injectFocus(focused == JNI_TRUE);
    if (focused) {
        // A window that has just been added is in touch mode until the first
        // hardware key event.  The dispatcher normally sends this alongside
        // focus; ViewRootImpl uses it for focus highlight and scrolling.
        droidlet::injectTouchMode(true);
    }
}

JNIEXPORT jboolean JNICALL
Java_android_app_DroidletLauncher_nativeHostWaitFrame(JNIEnv*, jclass, jint timeoutMs) {
    return droidlet::waitHostFrame(timeoutMs > 0 ? timeoutMs : 0) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_android_app_DroidletLauncher_nativeInputScriptFinished(JNIEnv*, jclass) {
    return droidlet::inputScriptFinished() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_android_app_DroidletLauncher_nativeHostFrameWidth(JNIEnv*, jclass) {
    return droidlet::hostFrameWidth();
}

JNIEXPORT jint JNICALL
Java_android_app_DroidletLauncher_nativeHostFrameHeight(JNIEnv*, jclass) {
    return droidlet::hostFrameHeight();
}

JNIEXPORT void JNICALL
Java_android_app_DroidletLauncher_nativeHostRemoveLayer(JNIEnv*, jclass, jint layerId) {
    droidlet::hostRemoveLayer(layerId);
}

JNIEXPORT jfloatArray JNICALL
Java_android_app_DroidletLauncher_nativeHostLayerPosition(JNIEnv* env, jclass, jint layerId) {
    float x = 0.0f;
    float y = 0.0f;
    if (!droidlet::hostLayerPosition(layerId, &x, &y)) {
        return nullptr;
    }
    jfloatArray result = env->NewFloatArray(2);
    if (result != nullptr) {
        const jfloat values[2] = {x, y};
        env->SetFloatArrayRegion(result, 0, 2, values);
    }
    return result;
}

JNIEXPORT jlongArray JNICALL
Java_android_app_DroidletLauncher_nativeHostFrameStats(JNIEnv* env, jclass) {
    uint64_t latched = 0;
    uint64_t presented = 0;
    uint64_t dropped = 0;
    droidlet::hostFrameStats(&latched, &presented, &dropped);
    jlongArray result = env->NewLongArray(3);
    if (result == nullptr) {
        return nullptr;
    }
    const jlong values[3] = {static_cast<jlong>(latched), static_cast<jlong>(presented),
                             static_cast<jlong>(dropped)};
    env->SetLongArrayRegion(result, 0, 3, values);
    return result;
}

JNIEXPORT jint JNICALL
Java_android_app_DroidletLauncher_nativeHostCopyFrame(JNIEnv* env, jclass, jintArray pixels) {
    if (pixels == nullptr) {
        return -1;
    }
    const int width = droidlet::hostFrameWidth();
    const int height = droidlet::hostFrameHeight();
    if (width <= 0 || height <= 0) {
        return -1;
    }
    const jsize expected = width * height;
    if (env->GetArrayLength(pixels) < expected) {
        return -1;
    }

    static thread_local std::vector<uint32_t> frame;
    frame.resize(static_cast<size_t>(expected));
    const int copied = droidlet::copyHostFrame(frame.data(), expected);
    if (copied <= 0) {
        return -1;
    }
    env->SetIntArrayRegion(pixels, 0, copied, reinterpret_cast<jint*>(frame.data()));
    return copied;
}

}  // extern "C"
