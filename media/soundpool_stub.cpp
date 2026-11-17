/*
 * Silent android.media.SoundPool backend for the host.
 *
 * SoundPool's static initializer does System.loadLibrary("soundpool").  The
 * host has no short-sample audio engine, and without this library the whole
 * class fails to initialize, so any app that builds a SoundPool in
 * Application.onCreate dies before its first activity (Edge Roll is one).
 *
 * Every entry point is accepted: loads complete successfully, play returns a
 * stream id instead of 0 (0 means "failed" to the Java API), and playback is
 * silent.
 */

#include <jni.h>
#include <log/log.h>

#include <atomic>
#include <mutex>

namespace {

std::mutex gLock;
jobject gSoundPool = nullptr;  // global ref to the SoundPool instance
std::atomic<jint> gNextSample{1};
std::atomic<jint> gNextStream{1};

// SoundPool.SAMPLE_LOADED
constexpr jint kSampleLoaded = 1;

void postSampleLoaded(JNIEnv* env, jint sampleId) {
    jobject target = nullptr;
    {
        std::lock_guard lock(gLock);
        target = gSoundPool;
    }
    if (target == nullptr) {
        return;
    }
    jclass clazz = env->GetObjectClass(target);
    if (clazz == nullptr) {
        return;
    }
    jmethodID method = env->GetMethodID(clazz, "postEventFromNative",
                                        "(IIILjava/lang/Object;)V");
    if (method != nullptr) {
        env->CallVoidMethod(target, method, kSampleLoaded, sampleId, 0, nullptr);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(clazz);
}

jint soundPoolSetup(JNIEnv* env, jobject thiz, jint /*maxStreams*/,
                    jobject /*attributes*/, jstring /*opPackageName*/) {
    std::lock_guard lock(gLock);
    if (gSoundPool != nullptr) {
        env->DeleteGlobalRef(gSoundPool);
    }
    gSoundPool = env->NewGlobalRef(thiz);
    ALOGV("soundpool: stub setup");
    return 0;
}

void soundPoolRelease(JNIEnv* env, jobject /*thiz*/) {
    std::lock_guard lock(gLock);
    if (gSoundPool != nullptr) {
        env->DeleteGlobalRef(gSoundPool);
        gSoundPool = nullptr;
    }
}

jint soundPoolLoad(JNIEnv* env, jobject /*thiz*/, jobject /*fd*/, jlong /*offset*/,
                   jlong /*length*/, jint /*priority*/) {
    const jint id = gNextSample.fetch_add(1);
    postSampleLoaded(env, id);
    return id;
}

jboolean soundPoolUnload(JNIEnv*, jobject, jint) {
    return JNI_TRUE;
}

jint soundPoolPlay(JNIEnv*, jobject, jint /*soundId*/, jfloat /*leftVolume*/,
                   jfloat /*rightVolume*/, jint /*priority*/, jint /*loop*/,
                   jfloat /*rate*/, jint /*playerIId*/) {
    return gNextStream.fetch_add(1);
}

// pause/resume/autoPause/autoResume/stop/setVolume/mute/setPriority/setLoop/
// setRate have no host audio to act on.
void soundPoolNoop() {}

JNINativeMethod gMethods[] = {
        {"_load", "(Ljava/io/FileDescriptor;JJI)I",
         reinterpret_cast<void*>(soundPoolLoad)},
        {"unload", "(I)Z", reinterpret_cast<void*>(soundPoolUnload)},
        {"_play", "(IFFIIFI)I", reinterpret_cast<void*>(soundPoolPlay)},
        {"pause", "(I)V", reinterpret_cast<void*>(soundPoolNoop)},
        {"resume", "(I)V", reinterpret_cast<void*>(soundPoolNoop)},
        {"autoPause", "()V", reinterpret_cast<void*>(soundPoolNoop)},
        {"autoResume", "()V", reinterpret_cast<void*>(soundPoolNoop)},
        {"stop", "(I)V", reinterpret_cast<void*>(soundPoolNoop)},
        {"_setVolume", "(IFF)V", reinterpret_cast<void*>(soundPoolNoop)},
        {"_mute", "(Z)V", reinterpret_cast<void*>(soundPoolNoop)},
        {"setPriority", "(II)V", reinterpret_cast<void*>(soundPoolNoop)},
        {"setLoop", "(II)V", reinterpret_cast<void*>(soundPoolNoop)},
        {"setRate", "(IF)V", reinterpret_cast<void*>(soundPoolNoop)},
        {"native_setup", "(ILjava/lang/Object;Ljava/lang/String;)I",
         reinterpret_cast<void*>(soundPoolSetup)},
        {"native_release", "()V", reinterpret_cast<void*>(soundPoolRelease)},
};

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        ALOGE("soundpool: GetEnv failed");
        return JNI_ERR;
    }
    jclass clazz = env->FindClass("android/media/SoundPool");
    if (clazz == nullptr) {
        ALOGE("soundpool: android/media/SoundPool not found");
        return JNI_ERR;
    }
    if (env->RegisterNatives(clazz, gMethods,
                             sizeof(gMethods) / sizeof(gMethods[0])) != 0) {
        ALOGE("soundpool: RegisterNatives failed");
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}
