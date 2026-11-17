/*
 * Host AudioTrack backend for droidlet.
 *
 * The stock android.media.AudioTrack class is a thin Java wrapper over native
 * methods.  We implement the playback subset with a host PulseAudio/PipeWire
 * sink, falling back to ALSA and then a paced null sink.  PCM input is
 * converted to S16 interleaved stereo/multichannel for the sink.
 */

#include <jni.h>

#include <log/log.h>
#include <time.h>
#include <alsa/asoundlib.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <atomic>
#include <thread>
#include <vector>

#include "audio_sink.h"

namespace {

constexpr int kEncodingPcm16 = 2;
constexpr int kEncodingPcm8 = 3;
constexpr int kEncodingPcmFloat = 4;
constexpr int kSuccess = 0;
constexpr int kErrorBadValue = -2;
constexpr int kErrorInvalidOperation = -3;
constexpr int kErrorDeadObject = -6;

struct TrackState {
    HostAudioSink sink;
    int sampleRate = 48000;
    int channels = 2;
    int encoding = kEncodingPcm16;
    int bufferFrames = 4096;
    int64_t framesWritten = 0;
    bool playing = false;
    std::chrono::steady_clock::time_point nullClock;
    int64_t nullStartFrame = 0;
};

jfieldID gNativeTrackField = nullptr;

TrackState* getState(JNIEnv* env, jobject thiz) {
    return reinterpret_cast<TrackState*>(env->GetLongField(thiz, gNativeTrackField));
}

void setState(JNIEnv* env, jobject thiz, TrackState* state) {
    env->SetLongField(thiz, gNativeTrackField, reinterpret_cast<jlong>(state));
}

bool writeFrames(TrackState* state, const int16_t* samples, int frames) {
    if (frames <= 0) {
        return true;
    }
    if (state->sink.isNull()) {
        // Null sink: keep the position moving at the nominal rate.
        const auto now = std::chrono::steady_clock::now();
        const int64_t elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now - state->nullClock).count();
        const int64_t targetFrame = state->nullStartFrame +
                (elapsedUs * state->sampleRate) / 1000000;
        if (targetFrame > state->framesWritten) {
            state->framesWritten = targetFrame;
        }
    }
    if (!state->sink.write(samples, frames)) {
        return false;
    }
    state->framesWritten += frames;
    return true;
}

int writeShorts(JNIEnv* env, jobject thiz, jshortArray array, jint offset, jint size) {
    TrackState* state = getState(env, thiz);
    if (state == nullptr || array == nullptr) {
        return kErrorInvalidOperation;
    }
    jshort* elements = env->GetShortArrayElements(array, nullptr);
    if (elements == nullptr) {
        return kErrorBadValue;
    }
    std::vector<int16_t> samples(static_cast<size_t>(size));
    std::memcpy(samples.data(), elements + offset, static_cast<size_t>(size) * sizeof(int16_t));
    env->ReleaseShortArrayElements(array, elements, JNI_ABORT);
    const int frames = size / std::max(1, state->channels);
    return writeFrames(state, samples.data(), frames) ? size : kErrorDeadObject;
}

int writeBytes(JNIEnv* env, jobject thiz, jbyteArray array, jint offset, jint size) {
    TrackState* state = getState(env, thiz);
    if (state == nullptr || array == nullptr) {
        return kErrorInvalidOperation;
    }
    jbyte* elements = env->GetByteArrayElements(array, nullptr);
    if (elements == nullptr) {
        return kErrorBadValue;
    }
    std::vector<int16_t> samples;
    if (state->encoding == kEncodingPcm16) {
        samples.resize(static_cast<size_t>(size) / 2);
        std::memcpy(samples.data(), elements + offset, static_cast<size_t>(size));
    } else if (state->encoding == kEncodingPcm8) {
        samples.resize(static_cast<size_t>(size));
        for (int i = 0; i < size; i++) {
            samples[i] = static_cast<int16_t>(
                    (static_cast<int>(elements[offset + i]) - 128) << 8);
        }
    } else {
        env->ReleaseByteArrayElements(array, elements, JNI_ABORT);
        return kErrorInvalidOperation;
    }
    env->ReleaseByteArrayElements(array, elements, JNI_ABORT);
    const int frames = static_cast<int>(samples.size()) / std::max(1, state->channels);
    return writeFrames(state, samples.data(), frames) ? size : kErrorDeadObject;
}

int writeFloats(JNIEnv* env, jobject thiz, jfloatArray array, jint offset, jint size) {
    TrackState* state = getState(env, thiz);
    if (state == nullptr || array == nullptr) {
        return kErrorInvalidOperation;
    }
    jfloat* elements = env->GetFloatArrayElements(array, nullptr);
    if (elements == nullptr) {
        return kErrorBadValue;
    }
    std::vector<int16_t> samples(static_cast<size_t>(size));
    for (int i = 0; i < size; i++) {
        float sample = std::clamp(elements[offset + i], -1.0f, 1.0f);
        samples[i] = static_cast<int16_t>(sample * 32767.0f);
    }
    env->ReleaseFloatArrayElements(array, elements, JNI_ABORT);
    const int frames = size / std::max(1, state->channels);
    return writeFrames(state, samples.data(), frames) ? size : kErrorDeadObject;
}

int writeNativeBytes(JNIEnv* env, jobject thiz, jobject buffer, jint position, jint size) {
    TrackState* state = getState(env, thiz);
    if (state == nullptr || buffer == nullptr) {
        return kErrorInvalidOperation;
    }
    auto* address = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    if (address == nullptr) {
        return kErrorBadValue;
    }
    std::vector<int16_t> samples;
    if (state->encoding == kEncodingPcm16) {
        samples.resize(static_cast<size_t>(size) / 2);
        std::memcpy(samples.data(), address + position, static_cast<size_t>(size));
    } else if (state->encoding == kEncodingPcm8) {
        samples.resize(static_cast<size_t>(size));
        for (int i = 0; i < size; i++) {
            samples[i] = static_cast<int16_t>((static_cast<int>(address[position + i]) - 128) << 8);
        }
    } else {
        return kErrorInvalidOperation;
    }
    const int frames = static_cast<int>(samples.size()) / std::max(1, state->channels);
    return writeFrames(state, samples.data(), frames) ? size : kErrorDeadObject;
}

jint nativeSetup(JNIEnv* env, jobject thiz, jobject weakRef, jobject /*attributes*/,
                 jintArray sampleRateArray, jobject channelMasks, jint audioFormat,
                 jint bufferSizeBytes, jint mode, jintArray sessionIdArray,
                 jobject /*attributionSource*/, jlong /*nativeTrack*/, jboolean /*offload*/,
                 jint /*encapsulationMode*/, jobject /*tunerConfiguration*/,
                 jstring /*opPackageName*/, jstring /*codecProvenance*/) {
    (void)weakRef;
    (void)mode;
    auto* state = new TrackState();
    state->encoding = audioFormat;
    int channels = 2;
    if (channelMasks != nullptr) {
        jclass masksClass = env->GetObjectClass(channelMasks);
        jmethodID getChannelCount = env->GetMethodID(masksClass, "getChannelCount", "()I");
        if (getChannelCount != nullptr) {
            channels = env->CallIntMethod(channelMasks, getChannelCount);
        }
    }
    state->channels = std::max(1, channels);
    int sampleRate = 48000;
    if (sampleRateArray != nullptr) {
        jint value = 0;
        env->GetIntArrayRegion(sampleRateArray, 0, 1, &value);
        if (value > 0) {
            sampleRate = value;
        }
        env->SetIntArrayRegion(sampleRateArray, 0, 1, &sampleRate);
    }
    state->sampleRate = sampleRate;
    const int bytesPerFrame = state->channels *
            (state->encoding == kEncodingPcmFloat ? 4 : (state->encoding == kEncodingPcm16 ? 2 : 1));
    state->bufferFrames = bufferSizeBytes > 0
            ? std::max(256, bufferSizeBytes / std::max(1, bytesPerFrame))
            : sampleRate / 10;
    state->sink.open("droidlet-audiotrack", state->sampleRate, state->channels,
                     std::getenv("DROIDLET_AUDIOTRACK_AUDIO"), true);
    if (state->sink.isNull()) {
        state->nullClock = std::chrono::steady_clock::now();
        state->nullStartFrame = 0;
    }
    setState(env, thiz, state);
    if (sessionIdArray != nullptr) {
        jint session = 1;
        env->SetIntArrayRegion(sessionIdArray, 0, 1, &session);
    }
    __android_log_print(ANDROID_LOG_INFO, "droidlet-audiotrack",
                        "setup %d Hz %d ch encoding=%d buffer=%d frames", state->sampleRate,
                        state->channels, state->encoding, state->bufferFrames);
    return kSuccess;
}

void nativeFinalize(JNIEnv* env, jobject thiz) {
    TrackState* state = getState(env, thiz);
    if (state != nullptr) {
        state->sink.close();
        delete state;
        setState(env, thiz, nullptr);
    }
}

void nativeStart(JNIEnv* env, jobject thiz) {
    TrackState* state = getState(env, thiz);
    if (state != nullptr) {
        state->playing = true;
    }
}

void nativeStop(JNIEnv* env, jobject thiz) {
    TrackState* state = getState(env, thiz);
    if (state != nullptr) {
        state->playing = false;
        state->sink.flush();
    }
}

void nativePause(JNIEnv* env, jobject thiz) {
    TrackState* state = getState(env, thiz);
    if (state != nullptr) {
        state->playing = false;
    }
}

void nativeFlush(JNIEnv* env, jobject thiz) {
    TrackState* state = getState(env, thiz);
    if (state != nullptr) {
        state->sink.flush();
        state->framesWritten = 0;
        state->nullStartFrame = 0;
        state->nullClock = std::chrono::steady_clock::now();
    }
}

void nativeRelease(JNIEnv* env, jobject thiz) {
    nativeFinalize(env, thiz);
}

int64_t playedFrames(const TrackState* state) {
    int64_t frames = state->framesWritten;
    if (frames > 0 && state->sink.isOpen() && !state->sink.isNull()) {
        const int64_t latency = state->sink.latencyFrames();
        if (latency > 0) {
            frames = std::max<int64_t>(0, frames - latency);
        }
    }
    return frames;
}

jint getPosition(JNIEnv* env, jobject thiz) {
    TrackState* state = getState(env, thiz);
    if (state == nullptr) {
        return 0;
    }
    if (state->sink.isNull() && state->playing) {
        const auto now = std::chrono::steady_clock::now();
        const int64_t elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now - state->nullClock).count();
        const int64_t target = state->nullStartFrame +
                (elapsedUs * state->sampleRate) / 1000000;
        if (target > state->framesWritten) {
            state->framesWritten = target;
        }
    }
    return static_cast<jint>(playedFrames(state));
}

jint setPosition(JNIEnv* env, jobject thiz, jint position) {
    TrackState* state = getState(env, thiz);
    if (state == nullptr) {
        return kErrorInvalidOperation;
    }
    state->framesWritten = position;
    state->nullStartFrame = position;
    state->nullClock = std::chrono::steady_clock::now();
    state->sink.flush();
    return kSuccess;
}

jint getBufferSizeFrames(JNIEnv* env, jobject thiz) {
    TrackState* state = getState(env, thiz);
    return state != nullptr ? state->bufferFrames : 0;
}

jint setBufferSizeFrames(JNIEnv* env, jobject thiz, jint frames) {
    TrackState* state = getState(env, thiz);
    if (state != nullptr && frames > 0) {
        state->bufferFrames = frames;
    }
    return state != nullptr ? state->bufferFrames : 0;
}

void setVolume(JNIEnv* env, jobject thiz, jfloat left, jfloat right) {
    TrackState* state = getState(env, thiz);
    if (state != nullptr) {
        state->sink.setVolume(left, right);
    }
}

jint getPlaybackRate(JNIEnv* env, jobject thiz) {
    TrackState* state = getState(env, thiz);
    return state != nullptr ? state->sampleRate : 48000;
}

jint setPlaybackRate(JNIEnv* env, jobject thiz, jint rate) {
    TrackState* state = getState(env, thiz);
    if (state != nullptr && rate > 0) {
        state->sampleRate = rate;
    }
    return kSuccess;
}

jint getTimestamp(JNIEnv*, jobject, jlongArray) {
    // Do not advertise a timestamp.  The host sink reports its position from
    // the shared audio thread and its latency estimate is not stable enough
    // for ExoPlayer's exact timestamp-continuity check; without timestamps it
    // falls back to getPlaybackHeadPosition and its own smoothing.
    return kErrorInvalidOperation;
}

jint getMinBufferSize(JNIEnv*, jclass, jint sampleRate, jint channelCount, jint /*format*/) {
    return std::max(256, (sampleRate / 10) * std::max(1, static_cast<int>(channelCount)) * 2);
}

jint getOutputSampleRate(JNIEnv*, jclass, jint /*streamType*/) {
    return 48000;
}

jboolean directOutputSupported(JNIEnv*, jclass, jint, jint, jobject, jint, jint, jint) {
    return JNI_FALSE;
}

void noOpVoid(JNIEnv*, jobject) {}
void noOpVoidInt(JNIEnv*, jobject, jint) {}
void noOpVoidFloat(JNIEnv*, jobject, jfloat) {}
void noOpVoidBoolean(JNIEnv*, jobject, jboolean) {}
void noOpVoidLong(JNIEnv*, jobject, jlong) {}
void noOpVoidObject(JNIEnv*, jobject, jobject) {}
void noOpVoidString(JNIEnv*, jobject, jstring) {}
void noOpVoidTwoInts(JNIEnv*, jobject, jint, jint) {}
void noOpVoidTwoFloats(JNIEnv*, jobject, jfloat, jfloat) {}
int zeroInt(JNIEnv*, jobject) { return 0; }
int zeroIntInt(JNIEnv*, jobject, jint) { return 0; }
int zeroIntTwoInts(JNIEnv*, jobject, jint, jint) { return 0; }
int zeroIntThreeInts(JNIEnv*, jobject, jint, jint, jint) { return 0; }
int zeroIntFloat(JNIEnv*, jobject, jfloat) { return 0; }
int zeroIntLong(JNIEnv*, jobject, jlong) { return 0; }
jlong zeroLong(JNIEnv*, jobject) { return 0; }
jboolean falseBoolean(JNIEnv*, jobject) { return JNI_FALSE; }
jboolean falseBooleanInt(JNIEnv*, jobject, jint) { return JNI_FALSE; }
jobject nullObject(JNIEnv*, jobject) { return nullptr; }
jobject nullObjectInt(JNIEnv*, jobject, jint) { return nullptr; }
jintArray nullIntArray(JNIEnv*, jobject) { return nullptr; }
jint zeroIntTwoObjects(JNIEnv*, jobject, jobject, jobject) { return 0; }
jint zeroIntFloatArray(JNIEnv*, jobject, jfloatArray) { return 0; }
jint zeroIntIntArray(JNIEnv*, jobject, jintArray) { return 0; }
jint writeNativeBytesCompat(JNIEnv* env, jobject thiz, jobject buffer, jint position, jint size,
                            jint /*format*/, jboolean /*blocking*/) {
    return writeNativeBytes(env, thiz, buffer, position, size);
}

jint writeShortsNative(JNIEnv* env, jobject thiz, jshortArray array, jint offset, jint size,
                       jint /*format*/, jboolean /*blocking*/) {
    return writeShorts(env, thiz, array, offset, size);
}

jint writeBytesNative(JNIEnv* env, jobject thiz, jbyteArray array, jint offset, jint size,
                      jint /*format*/, jboolean /*blocking*/) {
    return writeBytes(env, thiz, array, offset, size);
}

jint writeFloatsNative(JNIEnv* env, jobject thiz, jfloatArray array, jint offset, jint size,
                       jint /*format*/, jboolean /*blocking*/) {
    return writeFloats(env, thiz, array, offset, size);
}

jint audioSystemMaxChannels(JNIEnv*, jclass) { return 8; }
jint audioSystemMaxSampleRate(JNIEnv*, jclass) { return 192000; }
jint audioSystemMinSampleRate(JNIEnv*, jclass) { return 8000; }
jint audioSystemPrimarySampleRate(JNIEnv*, jclass) { return 48000; }
jint audioSystemPrimaryFrameCount(JNIEnv*, jclass) { return 1024; }
jint audioSystemOutputLatency(JNIEnv*, jclass, jint) { return 0; }
jint audioSystemNewId(JNIEnv*, jclass) {
    static std::atomic<jint> nextId{1};
    return nextId++;
}
jboolean audioSystemFalseBoolean(JNIEnv*, jclass) { return JNI_FALSE; }
jboolean audioSystemFalseInt(JNIEnv*, jclass, jint) { return JNI_FALSE; }
jint audioSystemZero(JNIEnv*, jclass) { return 0; }
jint audioSystemZeroInt(JNIEnv*, jclass, jint) { return 0; }
jint audioSystemZeroBoolean(JNIEnv*, jclass, jboolean) { return 0; }
jint audioSystemZeroFloat(JNIEnv*, jclass, jfloat) { return 0; }
jfloat audioSystemOneFloat(JNIEnv*, jclass) { return 1.0f; }
jint audioSystemSetParameters(JNIEnv*, jclass, jstring) { return 0; }
jstring audioSystemGetParameters(JNIEnv*, jclass, jstring) { return nullptr; }
jint audioSystemDevices(JNIEnv*, jclass, jobject, jint, jboolean, jobjectArray) { return 0; }
jint audioSystemStreamActive(JNIEnv*, jclass, jint, jint) { return 0; }

jint audioSystemListPorts(JNIEnv* env, jclass, jobject /*ports*/, jintArray generation) {
    if (generation != nullptr) {
        jint value = 1;
        env->SetIntArrayRegion(generation, 0, 1, &value);
    }
    return 0;
}

jint audioSystemListPatches(JNIEnv* env, jclass, jobject /*patches*/, jintArray generation) {
    if (generation != nullptr) {
        jint value = 1;
        env->SetIntArrayRegion(generation, 0, 1, &value);
    }
    return 0;
}

jint audioSystemGetSupportedDeviceTypes(JNIEnv*, jclass, jint, jobject) { return 0; }
jint audioSystemGetDirectPlaybackSupport(JNIEnv*, jclass, jobject, jobject, jint) { return 0; }
jint audioSystemGetOffloadSupport(JNIEnv*, jclass, jint, jint, jobject, jint) { return 0; }
jint audioSystemGetMicrophones(JNIEnv*, jclass, jobject) { return 0; }
jint audioSystemGetSurroundFormats(JNIEnv*, jclass, jobject) { return 0; }
jint audioSystemGetReportedSurroundFormats(JNIEnv*, jclass, jobject) { return 0; }
jboolean audioSystemFalseBooleanNoArgs(JNIEnv*, jclass) { return JNI_FALSE; }
jint audioSystemCreateAudioPatch(JNIEnv*, jclass, jobjectArray, jobjectArray, jobjectArray) {
    return 0;
}
jint audioSystemReleaseAudioPatch(JNIEnv*, jclass, jobject) { return 0; }
jint audioSystemSetAudioPortConfig(JNIEnv*, jclass, jobject) { return 0; }

void audioPortEventHandlerSetup(JNIEnv*, jobject, jobject) {}
void audioPortEventHandlerFinalize(JNIEnv*, jobject) {}

const JNINativeMethod kAudioSystemMethods[] = {
        {"native_getMaxChannelCount", "()I", reinterpret_cast<void*>(audioSystemMaxChannels)},
        {"native_getMaxSampleRate", "()I", reinterpret_cast<void*>(audioSystemMaxSampleRate)},
        {"native_getMinSampleRate", "()I", reinterpret_cast<void*>(audioSystemMinSampleRate)},
        {"getPrimaryOutputSamplingRate", "()I",
         reinterpret_cast<void*>(audioSystemPrimarySampleRate)},
        {"getPrimaryOutputFrameCount", "()I",
         reinterpret_cast<void*>(audioSystemPrimaryFrameCount)},
        {"getOutputLatency", "(I)I", reinterpret_cast<void*>(audioSystemOutputLatency)},
        {"newAudioSessionId", "()I", reinterpret_cast<void*>(audioSystemNewId)},
        {"newAudioPlayerId", "()I", reinterpret_cast<void*>(audioSystemNewId)},
        {"newAudioRecorderId", "()I", reinterpret_cast<void*>(audioSystemNewId)},
        {"setParameters", "(Ljava/lang/String;)I",
         reinterpret_cast<void*>(audioSystemSetParameters)},
        {"getParameters", "(Ljava/lang/String;)Ljava/lang/String;",
         reinterpret_cast<void*>(audioSystemGetParameters)},
        {"getDevicesForAttributes",
         "(Landroid/media/AudioAttributes;IZ[Landroid/media/AudioDeviceAttributes;)I",
         reinterpret_cast<void*>(audioSystemDevices)},
        {"isStreamActive", "(II)Z", reinterpret_cast<void*>(audioSystemFalseInt)},
        {"isStreamActiveRemotely", "(II)Z", reinterpret_cast<void*>(audioSystemFalseInt)},
        {"isSourceActive", "(I)Z", reinterpret_cast<void*>(audioSystemFalseInt)},
        {"getForceUse", "(I)I", reinterpret_cast<void*>(audioSystemZeroInt)},
        {"setForceUse", "(II)I", reinterpret_cast<void*>(audioSystemZeroInt)},
        {"getMasterMute", "()Z", reinterpret_cast<void*>(audioSystemFalseBoolean)},
        {"setMasterMute", "(Z)I", reinterpret_cast<void*>(audioSystemZeroBoolean)},
        {"getMasterMono", "()Z", reinterpret_cast<void*>(audioSystemFalseBoolean)},
        {"setMasterMono", "(Z)I", reinterpret_cast<void*>(audioSystemZeroBoolean)},
        {"getMasterVolume", "()F", reinterpret_cast<void*>(audioSystemOneFloat)},
        {"setMasterVolume", "(F)I", reinterpret_cast<void*>(audioSystemZeroFloat)},
        {"checkAudioFlinger", "()I", reinterpret_cast<void*>(audioSystemZero)},
        {"listAudioPorts", "(Ljava/util/ArrayList;[I)I",
         reinterpret_cast<void*>(audioSystemListPorts)},
        {"listAudioPatches", "(Ljava/util/ArrayList;[I)I",
         reinterpret_cast<void*>(audioSystemListPatches)},
        {"getSupportedDeviceTypes", "(ILandroid/util/IntArray;)I",
         reinterpret_cast<void*>(audioSystemGetSupportedDeviceTypes)},
        {"createAudioPatch", "([Landroid/media/AudioPatch;[Landroid/media/AudioPortConfig;[Landroid/media/AudioPortConfig;)I",
         reinterpret_cast<void*>(audioSystemCreateAudioPatch)},
        {"releaseAudioPatch", "(Landroid/media/AudioPatch;)I",
         reinterpret_cast<void*>(audioSystemReleaseAudioPatch)},
        {"setAudioPortConfig", "(Landroid/media/AudioPortConfig;)I",
         reinterpret_cast<void*>(audioSystemSetAudioPortConfig)},
        {"getDirectPlaybackSupport",
         "(Landroid/media/AudioFormat;Landroid/media/AudioAttributes;I)I",
         reinterpret_cast<void*>(audioSystemGetDirectPlaybackSupport)},
        {"native_get_offload_support", "(IILjava/lang/Object;I)I",
         reinterpret_cast<void*>(audioSystemGetOffloadSupport)},
        {"getMicrophones", "(Ljava/util/ArrayList;)I",
         reinterpret_cast<void*>(audioSystemGetMicrophones)},
        {"getSurroundFormats", "(Ljava/util/Map;)I",
         reinterpret_cast<void*>(audioSystemGetSurroundFormats)},
        {"getReportedSurroundFormats", "(Ljava/util/ArrayList;)I",
         reinterpret_cast<void*>(audioSystemGetReportedSurroundFormats)},
        {"isHapticPlaybackSupported", "()Z",
         reinterpret_cast<void*>(audioSystemFalseBooleanNoArgs)},
        {"isUltrasoundSupported", "()Z",
         reinterpret_cast<void*>(audioSystemFalseBooleanNoArgs)},
};

const JNINativeMethod kAudioPortEventHandlerMethods[] = {
        {"native_setup", "(Ljava/lang/Object;)V",
         reinterpret_cast<void*>(audioPortEventHandlerSetup)},
        {"native_finalize", "()V",
         reinterpret_cast<void*>(audioPortEventHandlerFinalize)},
};

const JNINativeMethod kMethods[] = {
        {"native_setup",
         "(Ljava/lang/Object;Ljava/lang/Object;[ILjava/lang/Object;III[ILandroid/os/Parcel;JZILjava/lang/Object;Ljava/lang/String;Ljava/lang/String;)I",
         reinterpret_cast<void*>(nativeSetup)},
        {"native_finalize", "()V", reinterpret_cast<void*>(nativeFinalize)},
        {"native_release", "()V", reinterpret_cast<void*>(nativeRelease)},
        {"native_start", "()V", reinterpret_cast<void*>(nativeStart)},
        {"native_stop", "()V", reinterpret_cast<void*>(nativeStop)},
        {"native_pause", "()V", reinterpret_cast<void*>(nativePause)},
        {"native_flush", "()V", reinterpret_cast<void*>(nativeFlush)},
        {"native_write_short", "([SIIIZ)I", reinterpret_cast<void*>(writeShortsNative)},
        {"native_write_byte", "([BIIIZ)I", reinterpret_cast<void*>(writeBytesNative)},
        {"native_write_float", "([FIIIZ)I", reinterpret_cast<void*>(writeFloatsNative)},
        {"native_write_native_bytes", "(Ljava/nio/ByteBuffer;IIIZ)I",
         reinterpret_cast<void*>(writeNativeBytesCompat)},
        {"native_reload_static", "()I", reinterpret_cast<void*>(zeroInt)},
        {"native_get_buffer_size_frames", "()I", reinterpret_cast<void*>(getBufferSizeFrames)},
        {"native_set_buffer_size_frames", "(I)I", reinterpret_cast<void*>(setBufferSizeFrames)},
        {"native_get_buffer_capacity_frames", "()I",
         reinterpret_cast<void*>(getBufferSizeFrames)},
        {"native_setVolume", "(FF)V", reinterpret_cast<void*>(setVolume)},
        {"native_set_playback_rate", "(I)I", reinterpret_cast<void*>(setPlaybackRate)},
        {"native_get_playback_rate", "()I", reinterpret_cast<void*>(getPlaybackRate)},
        {"native_set_playback_params", "(Landroid/media/PlaybackParams;)V",
         reinterpret_cast<void*>(noOpVoidObject)},
        {"native_get_playback_params", "()Landroid/media/PlaybackParams;",
         reinterpret_cast<void*>(nullObject)},
        {"native_set_marker_pos", "(I)I", reinterpret_cast<void*>(zeroIntInt)},
        {"native_get_marker_pos", "()I", reinterpret_cast<void*>(zeroInt)},
        {"native_set_pos_update_period", "(I)I", reinterpret_cast<void*>(zeroIntInt)},
        {"native_get_pos_update_period", "()I", reinterpret_cast<void*>(zeroInt)},
        {"native_get_written_frames_count", "()J", reinterpret_cast<void*>(zeroLong)},
        {"native_set_position", "(I)I", reinterpret_cast<void*>(setPosition)},
        {"native_get_position", "()I", reinterpret_cast<void*>(getPosition)},
        {"native_get_latency", "()I", reinterpret_cast<void*>(zeroInt)},
        {"native_get_underrun_count", "()I", reinterpret_cast<void*>(zeroInt)},
        {"native_get_flags", "()I", reinterpret_cast<void*>(zeroInt)},
        {"native_get_timestamp", "([J)I", reinterpret_cast<void*>(getTimestamp)},
        {"native_set_loop", "(III)I", reinterpret_cast<void*>(zeroIntThreeInts)},
        {"native_get_output_sample_rate", "(I)I",
         reinterpret_cast<void*>(getOutputSampleRate)},
        {"native_get_min_buff_size", "(III)I", reinterpret_cast<void*>(getMinBufferSize)},
        {"native_attachAuxEffect", "(I)I", reinterpret_cast<void*>(zeroIntInt)},
        {"native_setAuxEffectSendLevel", "(F)I", reinterpret_cast<void*>(zeroIntFloat)},
        {"native_setOutputDevice", "(I)Z", reinterpret_cast<void*>(falseBooleanInt)},
        {"native_getRoutedDeviceIds", "()[I", reinterpret_cast<void*>(nullIntArray)},
        {"native_enableDeviceCallback", "()V", reinterpret_cast<void*>(noOpVoid)},
        {"native_disableDeviceCallback", "()V", reinterpret_cast<void*>(noOpVoid)},
        {"native_applyVolumeShaper",
         "(Landroid/media/VolumeShaper$Configuration;Landroid/media/VolumeShaper$Operation;)I",
         reinterpret_cast<void*>(zeroIntTwoObjects)},
        {"native_getVolumeShaperState", "(I)Landroid/media/VolumeShaper$State;",
         reinterpret_cast<void*>(nullObjectInt)},
        {"native_setPresentation", "(II)I", reinterpret_cast<void*>(zeroIntTwoInts)},
        {"native_getPortId", "()I", reinterpret_cast<void*>(zeroInt)},
        {"native_set_delay_padding", "(II)V", reinterpret_cast<void*>(noOpVoidTwoInts)},
        {"native_set_audio_description_mix_level_db", "(F)I",
         reinterpret_cast<void*>(zeroIntFloat)},
        {"native_get_audio_description_mix_level_db", "([F)I",
         reinterpret_cast<void*>(zeroIntFloatArray)},
        {"native_set_dual_mono_mode", "(I)I", reinterpret_cast<void*>(zeroIntInt)},
        {"native_get_dual_mono_mode", "([I)I", reinterpret_cast<void*>(zeroIntIntArray)},
        {"native_setLogSessionId", "(Ljava/lang/String;)V",
         reinterpret_cast<void*>(noOpVoidString)},
        {"native_setStartThresholdInFrames", "(I)I", reinterpret_cast<void*>(zeroIntInt)},
        {"native_getMetrics", "()Landroid/os/PersistableBundle;",
         reinterpret_cast<void*>(nullObject)},
        {"native_setPlayerIId", "(I)V", reinterpret_cast<void*>(noOpVoidInt)},
        {"native_is_direct_output_supported", "(IILjava/lang/Object;III)Z",
         reinterpret_cast<void*>(directOutputSupported)},
};

}  // namespace

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    jclass audioTrack = env->FindClass("android/media/AudioTrack");
    if (audioTrack == nullptr) {
        env->ExceptionClear();
        return JNI_VERSION_1_6;
    }
    gNativeTrackField = env->GetFieldID(audioTrack, "mNativeTrackInJavaObj", "J");
    if (env->RegisterNatives(audioTrack, kMethods,
                             sizeof(kMethods) / sizeof(kMethods[0])) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "droidlet-audiotrack",
                            "native registration failed");
        env->ExceptionClear();
    }
    jclass audioSystem = env->FindClass("android/media/AudioSystem");
    if (audioSystem != nullptr) {
        if (env->RegisterNatives(audioSystem, kAudioSystemMethods,
                                 sizeof(kAudioSystemMethods) /
                                         sizeof(kAudioSystemMethods[0])) != 0) {
            __android_log_print(ANDROID_LOG_ERROR, "droidlet-audiotrack",
                                "AudioSystem registration failed");
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    jclass audioPortHandler = env->FindClass("android/media/AudioPortEventHandler");
    if (audioPortHandler != nullptr) {
        if (env->RegisterNatives(audioPortHandler, kAudioPortEventHandlerMethods,
                                 sizeof(kAudioPortEventHandlerMethods) /
                                         sizeof(kAudioPortEventHandlerMethods[0])) != 0) {
            __android_log_print(ANDROID_LOG_ERROR, "droidlet-audiotrack",
                                "AudioPortEventHandler registration failed");
            env->ExceptionClear();
        }
    } else {
        env->ExceptionClear();
    }
    return JNI_VERSION_1_6;
}
