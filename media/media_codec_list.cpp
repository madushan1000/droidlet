/*
 * Host MediaCodecList backend.
 *
 * android.media.MediaCodecList asks native code for a flat list of codecs and
 * their capabilities.  We advertise the host's ffmpeg-backed decoders (see
 * codec_registry.h) so apps that enumerate codecs can find a decoder instead of
 * falling back to a bundled software codec or failing.
 */

#include <jni.h>

#define LOG_TAG "droidlet-codec"
#include <log/log.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "codec_registry.h"

namespace {

jobject boxInteger(JNIEnv* env, int value) {
    jclass cls = env->FindClass("java/lang/Integer");
    jmethodID valueOf = env->GetStaticMethodID(cls, "valueOf", "(I)Ljava/lang/Integer;");
    return env->CallStaticObjectMethod(cls, valueOf, value);
}

jint getCodecCount(JNIEnv*, jclass) {
    size_t count = 0;
    hostCodecs(&count);
    return static_cast<jint>(count);
}

jstring getCodecName(JNIEnv* env, jclass, jint index) {
    size_t count = 0;
    const HostCodecInfo* codecs = hostCodecs(&count);
    if (index < 0 || static_cast<size_t>(index) >= count) {
        return nullptr;
    }
    return env->NewStringUTF(codecs[index].name);
}

jint getAttributes(JNIEnv*, jclass, jint index) {
    size_t count = 0;
    const HostCodecInfo* codecs = hostCodecs(&count);
    if (index < 0 || static_cast<size_t>(index) >= count) {
        return 0;
    }
    return codecs[index].attributes;
}

jint getSecurityModel(JNIEnv*, jclass, jint) {
    // MediaCodecInfo.SECURITY_MODEL_SANDBOXED
    return 0;
}

jobjectArray getSupportedTypes(JNIEnv* env, jclass, jint index) {
    size_t count = 0;
    const HostCodecInfo* codecs = hostCodecs(&count);
    if (index < 0 || static_cast<size_t>(index) >= count) {
        return nullptr;
    }
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray types = env->NewObjectArray(1, stringClass, nullptr);
    jstring mime = env->NewStringUTF(codecs[index].mime);
    env->SetObjectArrayElement(types, 0, mime);
    env->DeleteLocalRef(mime);
    return types;
}

jobject getCodecCapabilities(JNIEnv* env, jclass, jint index, jstring type) {
    size_t count = 0;
    const HostCodecInfo* codecs = hostCodecs(&count);
    if (index < 0 || static_cast<size_t>(index) >= count || type == nullptr) {
        return nullptr;
    }
    const char* raw = env->GetStringUTFChars(type, nullptr);
    std::string mime = raw != nullptr ? raw : "";
    if (raw != nullptr) {
        env->ReleaseStringUTFChars(type, raw);
    }
    if (mime != codecs[index].mime) {
        return nullptr;
    }
    jclass capsClass = env->FindClass("android/media/MediaCodecInfo$CodecCapabilities");
    if (capsClass == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jmethodID create = env->GetStaticMethodID(
            capsClass, "createFromProfileLevel",
            "(Ljava/lang/String;II)Landroid/media/MediaCodecInfo$CodecCapabilities;");
    if (create == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jstring javaMime = env->NewStringUTF(codecs[index].mime);
    jobject caps = env->CallStaticObjectMethod(capsClass, create, javaMime,
                                               codecs[index].profile, codecs[index].level);
    env->DeleteLocalRef(javaMime);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return caps;
}

jobject getGlobalSettings(JNIEnv* env, jclass) {
    jclass mapClass = env->FindClass("java/util/HashMap");
    jmethodID constructor = env->GetMethodID(mapClass, "<init>", "()V");
    jmethodID put = env->GetMethodID(
            mapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    jobject map = env->NewObject(mapClass, constructor);
    jstring key = env->NewStringUTF("max-concurrent-instances");
    jobject value = boxInteger(env, 16);
    env->CallObjectMethod(map, put, key, value);
    env->DeleteLocalRef(key);
    env->DeleteLocalRef(value);
    return map;
}

jint findCodecByName(JNIEnv* env, jclass, jstring name) {
    if (name == nullptr) {
        return -1;
    }
    const char* raw = env->GetStringUTFChars(name, nullptr);
    std::string text = raw != nullptr ? raw : "";
    if (raw != nullptr) {
        env->ReleaseStringUTFChars(name, raw);
    }
    size_t count = 0;
    const HostCodecInfo* codecs = hostCodecs(&count);
    for (size_t i = 0; i < count; i++) {
        if (text == codecs[i].name) {
            return static_cast<jint>(i);
        }
    }
    return -1;
}

void nativeInit(JNIEnv*, jclass) {}

const JNINativeMethod kMethods[] = {
        {"native_init", "()V", reinterpret_cast<void*>(nativeInit)},
        {"native_getCodecCount", "()I", reinterpret_cast<void*>(getCodecCount)},
        {"getCodecName", "(I)Ljava/lang/String;", reinterpret_cast<void*>(getCodecName)},
        {"getCanonicalName", "(I)Ljava/lang/String;",
         reinterpret_cast<void*>(getCodecName)},
        {"getSecurityModel", "(I)I", reinterpret_cast<void*>(getSecurityModel)},
        {"getAttributes", "(I)I", reinterpret_cast<void*>(getAttributes)},
        {"getSupportedTypes", "(I)[Ljava/lang/String;",
         reinterpret_cast<void*>(getSupportedTypes)},
        {"getCodecCapabilities",
         "(ILjava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;",
         reinterpret_cast<void*>(getCodecCapabilities)},
        {"native_getGlobalSettings", "()Ljava/util/Map;",
         reinterpret_cast<void*>(getGlobalSettings)},
        {"findCodecByName", "(Ljava/lang/String;)I",
         reinterpret_cast<void*>(findCodecByName)},
};

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_android_media_MediaCodecList_native_1init(JNIEnv* env, jclass clazz) {
    const size_t count = sizeof(kMethods) / sizeof(kMethods[0]);
    for (size_t i = 0; i < count; i++) {
        if (env->RegisterNatives(clazz, &kMethods[i], 1) != 0) {
            ALOGE("MediaCodecList registration failed for %s", kMethods[i].name);
            env->ExceptionClear();
        }
    }
}
