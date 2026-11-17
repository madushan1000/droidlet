/*
 * Host MediaCodec decoder backend for droidlet.
 *
 * The stock android.media.MediaCodec class is a large Java state machine over
 * native methods.  This implements the decoder subset used by simple
 * video/audio players: configure from a MediaFormat key/value map,
 * input/output ByteBuffers, Surface output for video, PCM output for audio,
 * queue/dequeue and format reporting, in both synchronous and asynchronous
 * (Callback) modes.  Buffer-input encoders (AAC/Opus audio, H.264/HEVC video)
 * are supported; surface input and secure/tunneled modes are not.
 */

#include <jni.h>

#define LOG_TAG "droidlet-codec"
#include <log/log.h>

#include <android/native_window.h>
#include <dlfcn.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <mutex>
#include <set>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "codec_registry.h"

namespace {

constexpr int kBufferFlagKeyFrame = 1;
constexpr int kBufferFlagCodecConfig = 2;
constexpr int kBufferFlagEndOfStream = 4;
constexpr int kInfoTryAgainLater = -1;
constexpr int kInfoOutputFormatChanged = -2;

// MediaCodec private event constants and callback codes.
constexpr int kEventCallback = 1;
constexpr int kCallbackInputAvailable = 1;
constexpr int kCallbackOutputAvailable = 2;
constexpr int kCallbackOutputFormatChange = 4;
constexpr size_t kInputBufferSize = 1 << 20;
constexpr size_t kOutputBufferSize = 4 << 20;
constexpr int kBufferCount = 8;

struct FrameHolder {
    AVFrame* frame = nullptr;
    int64_t ptsUs = 0;
};

struct Codec {
    JavaVM* vm = nullptr;
    jobject weakSelf = nullptr;
    jclass codecClass = nullptr;
    jmethodID postEvent = nullptr;

    std::string mime;
    std::string codecName;
    bool encoder = false;
    int colorFormat = 0;
    int frameRate = 30;
    AVCodecContext* context = nullptr;
    const AVCodec* decoder = nullptr;
    AVBSFContext* bsf = nullptr;
    std::vector<uint8_t> extradata;
    bool extradataChecked = false;
    bool sampleFormatChecked = false;
    std::vector<uint8_t> parameterSets;
    bool parameterSetsSent = false;
    ANativeWindow* window = nullptr;
    SwsContext* scaler = nullptr;
    int scalerWidth = 0;
    int scalerHeight = 0;
    AVPixelFormat scalerFormat = AV_PIX_FMT_NONE;
    std::vector<uint8_t> scalerBuffer;
    SwrContext* resampler = nullptr;
    int sampleRate = 0;
    int channels = 0;
    int width = 0;
    int height = 0;

    jobject inputBuffers[kBufferCount] = {};
    jobject outputBuffers[kBufferCount] = {};
    bool inputBusy[kBufferCount] = {};
    std::deque<int> freeInputs;
    std::deque<int> freeOutputs;
    std::map<int, FrameHolder> pendingFrames;
    std::map<int, std::pair<int, int64_t>> pendingAudio;  // index -> (size, pts)
    std::map<int, std::pair<int, int64_t>> pendingEncoded;  // index -> (size, pts)
    std::map<int, int> pendingEncodedFlags;
    std::deque<int> readyOutputs;
    std::set<int> dequeuedOutputs;
    std::mutex mutex;

    jobject callback = nullptr;
    bool async = false;
    bool formatChangeSent = false;
    bool eosQueued = false;
    bool eosOutputSent = false;
    bool codecConfigQueued = false;

    bool started = false;
};

jfieldID gContextField = nullptr;
jclass gBufferInfoClass = nullptr;
jfieldID gInfoOffset = nullptr;
jfieldID gInfoSize = nullptr;
jfieldID gInfoPts = nullptr;
jfieldID gInfoFlags = nullptr;

using FromSurfaceFn = ANativeWindow* (*)(JNIEnv*, jobject);

FromSurfaceFn fromSurfaceFunction() {
    static FromSurfaceFn function = []() -> FromSurfaceFn {
        void* handle = dlopen("libandroid.so", RTLD_NOW | RTLD_GLOBAL);
        if (handle == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<FromSurfaceFn>(dlsym(handle, "ANativeWindow_fromSurface"));
    }();
    return function;
}

Codec* getCodec(JNIEnv* env, jobject thiz) {
    return reinterpret_cast<Codec*>(env->GetLongField(thiz, gContextField));
}

void setCodec(JNIEnv* env, jobject thiz, Codec* codec) {
    env->SetLongField(thiz, gContextField, reinterpret_cast<jlong>(codec));
}

AVCodecID codecIdForMime(const std::string& mime) {
    if (mime == "video/avc") return AV_CODEC_ID_H264;
    if (mime == "video/hevc") return AV_CODEC_ID_HEVC;
    if (mime == "video/mp4v-es") return AV_CODEC_ID_MPEG4;
    if (mime == "video/3gpp") return AV_CODEC_ID_H263;
    if (mime == "video/x-vnd.on2.vp8") return AV_CODEC_ID_VP8;
    if (mime == "video/x-vnd.on2.vp9") return AV_CODEC_ID_VP9;
    if (mime == "video/av01") return AV_CODEC_ID_AV1;
    if (mime == "audio/mp4a-latm") return AV_CODEC_ID_AAC;
    if (mime == "audio/mpeg") return AV_CODEC_ID_MP3;
    if (mime == "audio/opus") return AV_CODEC_ID_OPUS;
    if (mime == "audio/vorbis") return AV_CODEC_ID_VORBIS;
    if (mime == "audio/flac") return AV_CODEC_ID_FLAC;
    if (mime == "audio/raw") return AV_CODEC_ID_PCM_S16LE;
    return AV_CODEC_ID_NONE;
}

jobject boxInteger(JNIEnv* env, int value) {
    jclass cls = env->FindClass("java/lang/Integer");
    jmethodID valueOf = env->GetStaticMethodID(cls, "valueOf", "(I)Ljava/lang/Integer;");
    return env->CallStaticObjectMethod(cls, valueOf, value);
}

jobject makeFormatMap(JNIEnv* env, Codec* codec, bool input) {
    jclass mapClass = env->FindClass("java/util/HashMap");
    jmethodID constructor = env->GetMethodID(mapClass, "<init>", "()V");
    jmethodID put = env->GetMethodID(
            mapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    jobject map = env->NewObject(mapClass, constructor);
    auto putString = [&](const char* key, const char* value) {
        jstring k = env->NewStringUTF(key);
        jstring v = env->NewStringUTF(value);
        env->CallObjectMethod(map, put, k, v);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(v);
    };
    auto putInt = [&](const char* key, int value) {
        jstring k = env->NewStringUTF(key);
        jobject v = boxInteger(env, value);
        env->CallObjectMethod(map, put, k, v);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(v);
    };
    auto putBytes = [&](const char* key, const std::vector<uint8_t>& data) {
        jbyteArray array = env->NewByteArray(static_cast<jsize>(data.size()));
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(data.size()),
                                reinterpret_cast<const jbyte*>(data.data()));
        jclass bufferClass = env->FindClass("java/nio/ByteBuffer");
        jmethodID wrap = env->GetStaticMethodID(
                bufferClass, "wrap", "([B)Ljava/nio/ByteBuffer;");
        jobject buffer = env->CallStaticObjectMethod(bufferClass, wrap, array);
        jstring k = env->NewStringUTF(key);
        env->CallObjectMethod(map, put, k, buffer);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(buffer);
        env->DeleteLocalRef(array);
    };
    putString("mime", codec->mime.c_str());
    if (codec->width > 0) {
        putInt("width", codec->width);
        putInt("height", codec->height);
    }
    if (codec->sampleRate > 0) {
        putInt("sample-rate", codec->sampleRate);
        putInt("channel-count", codec->channels);
    }
    if (input && codec->context != nullptr) {
        putInt("max-input-size", 1 << 20);
    }
    if (!input && codec->encoder && !codec->extradata.empty()) {
        putBytes("csd-0", codec->extradata);
    }
    return map;
}

void fillBufferInfo(JNIEnv* env, jobject info, int offset, int size, int64_t ptsUs, int flags) {
    env->SetIntField(info, gInfoOffset, offset);
    env->SetIntField(info, gInfoSize, size);
    env->SetLongField(info, gInfoPts, ptsUs);
    env->SetIntField(info, gInfoFlags, flags);
}

jobject newBufferInfo(JNIEnv* env, int offset, int size, int64_t ptsUs, int flags) {
    jmethodID constructor = env->GetMethodID(gBufferInfoClass, "<init>", "()V");
    jobject info = env->NewObject(gBufferInfoClass, constructor);
    fillBufferInfo(env, info, offset, size, ptsUs, flags);
    return info;
}

void postEvent(JNIEnv* env, Codec* codec, jint what, jint arg1, jint arg2, jobject obj) {
    if (!codec->async || codec->weakSelf == nullptr || codec->postEvent == nullptr) {
        return;
    }
    env->CallVoidMethod(codec->weakSelf, codec->postEvent, what, arg1, arg2, obj);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void postInputAvailable(JNIEnv* env, Codec* codec, int index) {
    postEvent(env, codec, kEventCallback, kCallbackInputAvailable, index, nullptr);
}

void postFormatChange(JNIEnv* env, Codec* codec) {
    jobject format = makeFormatMap(env, codec, false);
    postEvent(env, codec, kEventCallback, kCallbackOutputFormatChange, 0, format);
    env->DeleteLocalRef(format);
}

void postOutputAvailable(JNIEnv* env, Codec* codec, int index, int size, int64_t ptsUs,
                         int flags) {
    jobject info = newBufferInfo(env, 0, size, ptsUs, flags);
    postEvent(env, codec, kEventCallback, kCallbackOutputAvailable, index, info);
    env->DeleteLocalRef(info);
}

void postReadyOutputs(JNIEnv* env, Codec* codec) {
    while (!codec->readyOutputs.empty()) {
        int index = codec->readyOutputs.front();
        codec->readyOutputs.pop_front();
        int size = 0;
        int flags = 0;
        int64_t ptsUs = 0;
        auto frame = codec->pendingFrames.find(index);
        auto audio = codec->pendingAudio.find(index);
        auto encoded = codec->pendingEncoded.find(index);
        if (frame != codec->pendingFrames.end()) {
            ptsUs = frame->second.ptsUs;
        } else if (audio != codec->pendingAudio.end()) {
            size = audio->second.first;
            ptsUs = audio->second.second;
        } else if (encoded != codec->pendingEncoded.end()) {
            size = encoded->second.first;
            ptsUs = encoded->second.second;
            flags = codec->pendingEncodedFlags[index];
        } else {
            continue;
        }
        codec->dequeuedOutputs.insert(index);
        postOutputAvailable(env, codec, index, size, ptsUs, flags);
    }
}

jobjectArray makeBufferArray(JNIEnv* env, jobject* buffers) {
    jclass byteBufferClass = env->FindClass("java/nio/ByteBuffer");
    jobjectArray array = env->NewObjectArray(kBufferCount, byteBufferClass, nullptr);
    for (int i = 0; i < kBufferCount; i++) {
        env->SetObjectArrayElement(array, i, buffers[i]);
    }
    return array;
}

void setLittleEndian(JNIEnv* env, jobject buffer) {
    jclass byteBufferClass = env->FindClass("java/nio/ByteBuffer");
    jclass byteOrderClass = env->FindClass("java/nio/ByteOrder");
    if (byteBufferClass == nullptr || byteOrderClass == nullptr) {
        env->ExceptionClear();
        return;
    }
    jfieldID littleEndian = env->GetStaticFieldID(
            byteOrderClass, "LITTLE_ENDIAN", "Ljava/nio/ByteOrder;");
    jmethodID orderMethod = env->GetMethodID(
            byteBufferClass, "order", "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;");
    if (orderMethod == nullptr) {
        env->ExceptionClear();
        orderMethod = env->GetMethodID(
                byteBufferClass, "order", "(Ljava/nio/ByteOrder;)Ljava/nio/Buffer;");
    }
    if (littleEndian == nullptr || orderMethod == nullptr) {
        env->ExceptionClear();
        return;
    }
    jobject order = env->GetStaticObjectField(byteOrderClass, littleEndian);
    env->CallObjectMethod(buffer, orderMethod, order);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (order != nullptr) {
        env->DeleteLocalRef(order);
    }
}

bool allocateBuffers(JNIEnv* env, Codec* codec) {
    if (codec->inputBuffers[0] != nullptr) {
        return true;
    }
    for (int i = 0; i < kBufferCount; i++) {
        jobject inputBuffer = env->NewDirectByteBuffer(
                static_cast<void*>(new uint8_t[kInputBufferSize]), kInputBufferSize);
        jobject outputBuffer = env->NewDirectByteBuffer(
                static_cast<void*>(new uint8_t[kOutputBufferSize]), kOutputBufferSize);
        setLittleEndian(env, inputBuffer);
        setLittleEndian(env, outputBuffer);
        codec->inputBuffers[i] = env->NewGlobalRef(inputBuffer);
        codec->outputBuffers[i] = env->NewGlobalRef(outputBuffer);
        env->DeleteLocalRef(inputBuffer);
        env->DeleteLocalRef(outputBuffer);
        codec->freeInputs.push_back(i);
        codec->freeOutputs.push_back(i);
    }
    return true;
}

void clearPending(Codec* codec) {
    for (auto& entry : codec->pendingFrames) {
        av_frame_free(&entry.second.frame);
    }
    codec->pendingFrames.clear();
    codec->pendingAudio.clear();
    codec->pendingEncoded.clear();
    codec->pendingEncodedFlags.clear();
    codec->readyOutputs.clear();
    codec->dequeuedOutputs.clear();
    codec->freeOutputs.clear();
    for (int i = 0; i < kBufferCount; i++) {
        codec->freeOutputs.push_back(i);
    }
    codec->freeInputs.clear();
    for (int i = 0; i < kBufferCount; i++) {
        codec->freeInputs.push_back(i);
        codec->inputBusy[i] = false;
    }
}

void closeCodec(Codec* codec) {
    if (codec == nullptr) {
        return;
    }
    clearPending(codec);
    if (codec->bsf != nullptr) {
        av_bsf_free(&codec->bsf);
    }
    if (codec->context != nullptr) {
        avcodec_free_context(&codec->context);
    }
    if (codec->scaler != nullptr) {
        sws_freeContext(codec->scaler);
        codec->scaler = nullptr;
    }
    if (codec->resampler != nullptr) {
        swr_free(&codec->resampler);
    }
    if (codec->window != nullptr) {
        ANativeWindow_release(codec->window);
        codec->window = nullptr;
    }
    if (codec->weakSelf != nullptr) {
        JNIEnv* env = nullptr;
        bool attached = false;
        if (codec->vm != nullptr &&
            codec->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            if (codec->vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            }
        }
        if (env != nullptr) {
            if (codec->callback != nullptr) {
                env->DeleteGlobalRef(codec->callback);
                codec->callback = nullptr;
            }
            env->DeleteGlobalRef(codec->weakSelf);
        }
        if (attached) {
            codec->vm->DetachCurrentThread();
        }
    }
    delete codec;
}

void presentFrame(Codec* codec, AVFrame* frame) {
    if (codec->window == nullptr || frame == nullptr) {
        return;
    }
    ANativeWindow_Buffer buffer;
    ARect dirty{0, 0, 0, 0};
    if (ANativeWindow_lock(codec->window, &buffer, &dirty) != 0) {
        ALOGE("ANativeWindow_lock failed");
        return;
    }
    const int bufWidth = buffer.width;
    const int bufHeight = buffer.height;
    const int srcWidth = frame->width;
    const int srcHeight = frame->height;
    if (srcWidth <= 0 || srcHeight <= 0 || bufWidth <= 0 || bufHeight <= 0) {
        ANativeWindow_unlockAndPost(codec->window);
        return;
    }
    double scale = std::min(static_cast<double>(bufWidth) / srcWidth,
                            static_cast<double>(bufHeight) / srcHeight);
    int dstWidth = std::max(1, static_cast<int>(srcWidth * scale));
    int dstHeight = std::max(1, static_cast<int>(srcHeight * scale));
    const int dstX = (bufWidth - dstWidth) / 2;
    const int dstY = (bufHeight - dstHeight) / 2;
    auto* base = static_cast<uint8_t*>(buffer.bits);
    const int32_t format = buffer.format;
    const size_t bytesPerPixel = format == 4 ? 2 : 4;
    for (int y = 0; y < bufHeight; y++) {
        uint8_t* row = base + static_cast<size_t>(y) * buffer.stride * bytesPerPixel;
        if (format == 4) {
            std::memset(row, 0, static_cast<size_t>(bufWidth) * 2);
        } else {
            auto* pixels = reinterpret_cast<uint32_t*>(row);
            const uint32_t black = format == 5 ? 0x000000FFu : 0xFF000000u;
            for (int x = 0; x < bufWidth; x++) {
                pixels[x] = black;
            }
        }
    }
    AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(frame->format);
    if (codec->scaler == nullptr || codec->scalerWidth != srcWidth ||
        codec->scalerHeight != srcHeight || codec->scalerFormat != sourceFormat) {
        if (codec->scaler != nullptr) {
            sws_freeContext(codec->scaler);
        }
        codec->scaler = sws_getContext(srcWidth, srcHeight, sourceFormat, dstWidth, dstHeight,
                                       AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        codec->scalerWidth = srcWidth;
        codec->scalerHeight = srcHeight;
        codec->scalerFormat = sourceFormat;
        codec->scalerBuffer.resize(static_cast<size_t>(dstWidth) * dstHeight * 4);
    }
    if (codec->scaler != nullptr) {
        uint8_t* dstData[1] = {codec->scalerBuffer.data()};
        int dstLinesize[1] = {dstWidth * 4};
        sws_scale(codec->scaler, frame->data, frame->linesize, 0, srcHeight, dstData,
                  dstLinesize);
        const int copyWidth = std::min(dstWidth, bufWidth - dstX);
        const int copyHeight = std::min(dstHeight, bufHeight - dstY);
        for (int y = 0; y < copyHeight; y++) {
            const uint8_t* src = codec->scalerBuffer.data() + static_cast<size_t>(y) * dstWidth * 4;
            if (format == 4) {
                auto* row = reinterpret_cast<uint16_t*>(
                        base + static_cast<size_t>(dstY + y) * buffer.stride * 2) + dstX;
                for (int x = 0; x < copyWidth; x++) {
                    row[x] = static_cast<uint16_t>(((src[x * 4 + 0] >> 3) << 11) |
                                                   ((src[x * 4 + 1] >> 2) << 5) |
                                                   (src[x * 4 + 2] >> 3));
                }
            } else if (format == 5) {
                auto* row = reinterpret_cast<uint8_t*>(
                        base + static_cast<size_t>(dstY + y) * buffer.stride * 4) + dstX * 4;
                for (int x = 0; x < copyWidth; x++) {
                    row[x * 4 + 0] = src[x * 4 + 2];
                    row[x * 4 + 1] = src[x * 4 + 1];
                    row[x * 4 + 2] = src[x * 4 + 0];
                    row[x * 4 + 3] = 0xFF;
                }
            } else {
                auto* row = reinterpret_cast<uint8_t*>(
                        base + static_cast<size_t>(dstY + y) * buffer.stride * 4) + dstX * 4;
                for (int x = 0; x < copyWidth; x++) {
                    row[x * 4 + 0] = src[x * 4 + 0];
                    row[x * 4 + 1] = src[x * 4 + 1];
                    row[x * 4 + 2] = src[x * 4 + 2];
                    row[x * 4 + 3] = 0xFF;
                }
            }
        }
    }
    ANativeWindow_unlockAndPost(codec->window);
}

bool initBitstreamFilter(Codec* codec) {
    if (codec->bsf != nullptr) {
        av_bsf_free(&codec->bsf);
    }
    const char* name = nullptr;
    if (codec->mime == "video/avc") {
        name = "h264_mp4toannexb";
    } else if (codec->mime == "video/hevc") {
        name = "hevc_mp4toannexb";
    }
    if (name == nullptr) {
        return false;
    }
    const AVBitStreamFilter* filter = av_bsf_get_by_name(name);
    if (filter == nullptr || av_bsf_alloc(filter, &codec->bsf) < 0) {
        return false;
    }
    avcodec_parameters_from_context(codec->bsf->par_in, codec->context);
    if (!codec->extradata.empty()) {
        codec->bsf->par_in->extradata = static_cast<uint8_t*>(
                av_mallocz(codec->extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        codec->bsf->par_in->extradata_size = static_cast<int>(codec->extradata.size());
        std::memcpy(codec->bsf->par_in->extradata, codec->extradata.data(),
                    codec->extradata.size());
    }
    if (av_bsf_init(codec->bsf) < 0) {
        av_bsf_free(&codec->bsf);
        return false;
    }
    return true;
}

void reopenDecoder(Codec* codec) {
    if (codec->decoder == nullptr) {
        return;
    }
    AVCodecContext* context = avcodec_alloc_context3(codec->decoder);
    if (context == nullptr) {
        return;
    }
    context->thread_count = 1;
    if (!codec->extradata.empty()) {
        context->extradata = static_cast<uint8_t*>(
                av_mallocz(codec->extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        context->extradata_size = static_cast<int>(codec->extradata.size());
        std::memcpy(context->extradata, codec->extradata.data(), codec->extradata.size());
    }
    if (avcodec_open2(context, codec->decoder, nullptr) < 0) {
        avcodec_free_context(&context);
        return;
    }
    avcodec_free_context(&codec->context);
    codec->context = context;
}

std::vector<uint8_t> extractExtradata(const std::vector<uint8_t>& data, bool hevc) {
    std::vector<uint8_t> result;
    size_t position = 0;
    while (position + 4 <= data.size()) {
        uint32_t length = (static_cast<uint32_t>(data[position]) << 24) |
                (static_cast<uint32_t>(data[position + 1]) << 16) |
                (static_cast<uint32_t>(data[position + 2]) << 8) |
                static_cast<uint32_t>(data[position + 3]);
        position += 4;
        if (length == 0 || position + length > data.size()) {
            break;
        }
        int type = hevc ? ((data[position] >> 1) & 0x3F) : (data[position] & 0x1F);
        bool wanted = hevc ? (type == 32 || type == 33 || type == 34) : (type == 7 || type == 8);
        if (wanted) {
            result.insert(result.end(), {0, 0, 0, 1});
            result.insert(result.end(), data.begin() + position,
                          data.begin() + position + length);
        }
        position += length;
    }
    return result;
}

void consumeFrame(JNIEnv* env, Codec* codec, AVFrame* frame, int64_t ptsUs) {
    if (codec->context == nullptr) {
        return;
    }
    if (codec->context->codec_type == AVMEDIA_TYPE_VIDEO) {
        int index = codec->freeOutputs.empty() ? -1 : codec->freeOutputs.front();
        if (index < 0) {
            return;
        }
        codec->freeOutputs.pop_front();
        FrameHolder holder;
        holder.frame = av_frame_alloc();
        if (holder.frame == nullptr) {
            codec->freeOutputs.push_back(index);
            return;
        }
        av_frame_ref(holder.frame, frame);
        holder.ptsUs = ptsUs;
        codec->pendingFrames[index] = holder;
        codec->readyOutputs.push_back(index);
    } else {
        int index = codec->freeOutputs.empty() ? -1 : codec->freeOutputs.front();
        if (index < 0) {
            return;
        }
        codec->freeOutputs.pop_front();
        int outSamples = frame->nb_samples + 256;
        auto* address = static_cast<uint8_t*>(env->GetDirectBufferAddress(
                codec->outputBuffers[index]));
        int produced = 0;
        if (address != nullptr && codec->resampler != nullptr) {
            const int capacity = static_cast<int>(kOutputBufferSize /
                    std::max(1, codec->channels * 2));
            outSamples = std::min(outSamples, capacity);
            uint8_t* outData[1] = {address};
            produced = swr_convert(codec->resampler, outData, outSamples,
                                   const_cast<const uint8_t**>(frame->extended_data),
                                   frame->nb_samples);
            if (produced < 0) {
                produced = 0;
            }
        }
        codec->pendingAudio[index] = {produced * codec->channels * 2, ptsUs};
        codec->readyOutputs.push_back(index);
    }
}

void flushDecoder(JNIEnv* env, Codec* codec) {
    if (codec->context == nullptr) {
        return;
    }
    AVPacket* empty = av_packet_alloc();
    if (empty == nullptr) {
        return;
    }
    avcodec_send_packet(codec->context, empty);
    av_packet_free(&empty);
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        return;
    }
    while (avcodec_receive_frame(codec->context, frame) >= 0) {
        int64_t framePts = frame->best_effort_timestamp;
        if (framePts == AV_NOPTS_VALUE) {
            framePts = 0;
        }
        consumeFrame(env, codec, frame, framePts);
        av_frame_unref(frame);
    }
    av_frame_free(&frame);
}

std::vector<std::vector<uint8_t>> splitAnnexB(const std::vector<uint8_t>& data) {
    std::vector<std::vector<uint8_t>> units;
    size_t position = 0;
    while (position + 3 < data.size()) {
        size_t start = position;
        if (data[start] == 0 && data[start + 1] == 0 && data[start + 2] == 1) {
            start += 3;
        } else if (data[start] == 0 && data[start + 1] == 0 && data[start + 2] == 0 &&
                   start + 3 < data.size() && data[start + 3] == 1) {
            start += 4;
        } else {
            position++;
            continue;
        }
        size_t end = data.size();
        for (size_t i = start; i + 3 < data.size(); i++) {
            if (data[i] == 0 && data[i + 1] == 0 &&
                (data[i + 2] == 1 ||
                 (data[i + 2] == 0 && i + 3 < data.size() && data[i + 3] == 1))) {
                end = i;
                break;
            }
        }
        if (end > start) {
            units.emplace_back(data.begin() + start, data.begin() + end);
        }
        position = end;
    }
    return units;
}

std::vector<uint8_t> annexBToAvcC(const std::vector<uint8_t>& data) {
    std::vector<std::vector<uint8_t>> units = splitAnnexB(data);
    std::vector<uint8_t> sps;
    std::vector<std::vector<uint8_t>> ppsList;
    for (const auto& unit : units) {
        if (unit.empty()) {
            continue;
        }
        const int type = unit[0] & 0x1F;
        if (type == 7 && sps.empty()) {
            sps = unit;
        } else if (type == 8) {
            ppsList.push_back(unit);
        }
    }
    if (sps.size() < 4 || ppsList.empty()) {
        return {};
    }
    std::vector<uint8_t> result;
    result.push_back(1);
    result.push_back(sps[1]);
    result.push_back(sps[2]);
    result.push_back(sps[3]);
    result.push_back(0xFF);
    result.push_back(static_cast<uint8_t>(0xE0 | 1));
    result.push_back(static_cast<uint8_t>(sps.size() >> 8));
    result.push_back(static_cast<uint8_t>(sps.size() & 0xFF));
    result.insert(result.end(), sps.begin(), sps.end());
    result.push_back(static_cast<uint8_t>(ppsList.size()));
    for (const auto& pps : ppsList) {
        result.push_back(static_cast<uint8_t>(pps.size() >> 8));
        result.push_back(static_cast<uint8_t>(pps.size() & 0xFF));
        result.insert(result.end(), pps.begin(), pps.end());
    }
    return result;
}

bool looksAnnexB(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && data[0] == 0 && data[1] == 0 &&
            (data[2] == 1 || (data[2] == 0 && data[3] == 1));
}

void decodeInput(JNIEnv* env, Codec* codec, const std::vector<uint8_t>& data, int64_t ptsUs,
                 int flags) {
    if (codec->context == nullptr) {
        return;
    }
    std::vector<uint8_t> prefixed;
    const std::vector<uint8_t>* payload = &data;
    if (codec->mime == "video/hevc" && !codec->parameterSetsSent &&
        !codec->parameterSets.empty() && looksAnnexB(data)) {

        prefixed.reserve(codec->parameterSets.size() + data.size());
        prefixed.insert(prefixed.end(), codec->parameterSets.begin(),
                        codec->parameterSets.end());
        prefixed.insert(prefixed.end(), data.begin(), data.end());
        payload = &prefixed;
        codec->parameterSetsSent = true;
    }
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        return;
    }
    av_new_packet(packet, static_cast<int>(payload->size()));
    std::memcpy(packet->data, payload->data(), payload->size());
    packet->pts = ptsUs;
    packet->dts = ptsUs;
    if ((flags & kBufferFlagKeyFrame) != 0) {
        packet->flags |= AV_PKT_FLAG_KEY;
    }
    if (!codec->sampleFormatChecked) {
        codec->sampleFormatChecked = true;
        if (codec->context->codec_type == AVMEDIA_TYPE_VIDEO && looksAnnexB(data) &&
            !codec->extradata.empty()) {
            // The CSD was avcC/hvcC (length-prefixed parameter sets) but the
            // samples use Annex B start codes, as YouTube's DASH streams do.
            // ffmpeg cannot mix the two: drop the extradata and let the
            // decoder read the in-band parameter sets.
            codec->extradata.clear();
            reopenDecoder(codec);
            if (codec->bsf != nullptr) {
                av_bsf_free(&codec->bsf);
                codec->bsf = nullptr;
            }
        }
    }
    if (!codec->extradataChecked && !looksAnnexB(data)) {
        codec->extradataChecked = true;
        std::vector<uint8_t> extra = extractExtradata(*payload, codec->mime == "video/hevc");
        if (!extra.empty()) {
            codec->extradata = std::move(extra);
            reopenDecoder(codec);
            initBitstreamFilter(codec);
        }
    }
    if (codec->bsf != nullptr && !looksAnnexB(*payload)) {
        int bsfSend = av_bsf_send_packet(codec->bsf, packet);
        int filteredCount = 0;
        if (bsfSend >= 0) {
            AVPacket* filtered = av_packet_alloc();
            while (av_bsf_receive_packet(codec->bsf, filtered) >= 0) {
                filteredCount++;
                int send = avcodec_send_packet(codec->context, filtered);
                if (send < 0) {
                    ALOGE("decode send failed=%d", send);
                }
                av_packet_unref(filtered);
            }
            av_packet_free(&filtered);
        }
        if (filteredCount == 0 && bsfSend < 0) {
            ALOGE("bsf send failed=%d", bsfSend);
        }
    } else {
        int send = avcodec_send_packet(codec->context, packet);
        if (send < 0) {
            ALOGE("decode send failed=%d", send);
        }
    }
    av_packet_free(&packet);
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        return;
    }
    while (avcodec_receive_frame(codec->context, frame) >= 0) {
        int64_t framePts = frame->best_effort_timestamp;
        if (framePts == AV_NOPTS_VALUE) {
            framePts = ptsUs;
        }
        consumeFrame(env, codec, frame, framePts);
        av_frame_unref(frame);
    }
    av_frame_free(&frame);
    if ((flags & kBufferFlagEndOfStream) != 0) {
        codec->eosQueued = true;
    }
}

void drainEncoder(JNIEnv* env, Codec* codec) {
    (void)env;
    if (codec->context == nullptr) {
        return;
    }
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        return;
    }
    while (avcodec_receive_packet(codec->context, packet) >= 0) {
        if (!codec->freeOutputs.empty()) {
            int index = codec->freeOutputs.front();
            codec->freeOutputs.pop_front();
            auto* address = static_cast<uint8_t*>(env->GetDirectBufferAddress(
                    codec->outputBuffers[index]));
            int size = std::min<int>(packet->size, static_cast<int>(kOutputBufferSize));
            if (address != nullptr && size > 0) {
                std::memcpy(address, packet->data, static_cast<size_t>(size));
            }
            int64_t ptsUs = 0;
            if (packet->pts != AV_NOPTS_VALUE) {
                ptsUs = av_rescale_q(packet->pts, codec->context->time_base, AV_TIME_BASE_Q);
            }
            codec->pendingEncoded[index] = {size, ptsUs};
            codec->pendingEncodedFlags[index] =
                    (packet->flags & AV_PKT_FLAG_KEY) != 0 ? kBufferFlagKeyFrame : 0;
            codec->readyOutputs.push_back(index);
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
}

void queueCodecConfig(JNIEnv* env, Codec* codec) {
    if (codec->codecConfigQueued || codec->extradata.empty() ||
        codec->freeOutputs.empty()) {
        return;
    }
    int index = codec->freeOutputs.front();
    codec->freeOutputs.pop_front();
    auto* address = static_cast<uint8_t*>(env->GetDirectBufferAddress(
            codec->outputBuffers[index]));
    int size = std::min<int>(static_cast<int>(codec->extradata.size()),
                             static_cast<int>(kOutputBufferSize));
    if (address != nullptr && size > 0) {
        std::memcpy(address, codec->extradata.data(), static_cast<size_t>(size));
    }
    codec->pendingEncoded[index] = {size, 0};
    codec->pendingEncodedFlags[index] = kBufferFlagCodecConfig;
    codec->readyOutputs.push_back(index);
    codec->codecConfigQueued = true;
}

void encodeFrame(JNIEnv* env, Codec* codec, const std::vector<uint8_t>& data,
                 int64_t ptsUs) {
    if (codec->context == nullptr || codec->context->codec_type == AVMEDIA_TYPE_UNKNOWN) {
        return;
    }
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        return;
    }
    frame->pts = av_rescale_q(ptsUs, AV_TIME_BASE_Q, codec->context->time_base);
    if (codec->context->codec_type == AVMEDIA_TYPE_AUDIO) {
        const int channels = std::max(1, codec->channels);
        const int samples = static_cast<int>(data.size() / (sizeof(int16_t) * channels));
        if (samples <= 0) {
            av_frame_free(&frame);
            return;
        }
        frame->nb_samples = samples;
        frame->format = codec->context->sample_fmt;
        frame->sample_rate = codec->context->sample_rate;
        av_channel_layout_copy(&frame->ch_layout, &codec->context->ch_layout);
        if (av_frame_get_buffer(frame, 0) < 0) {
            av_frame_free(&frame);
            return;
        }
        auto* input = reinterpret_cast<const int16_t*>(data.data());
        if (codec->context->sample_fmt == AV_SAMPLE_FMT_FLTP) {
            for (int channel = 0; channel < channels; channel++) {
                auto* out = reinterpret_cast<float*>(frame->data[channel]);
                for (int i = 0; i < samples; i++) {
                    out[i] = input[i * channels + channel] / 32768.0f;
                }
            }
        } else if (codec->context->sample_fmt == AV_SAMPLE_FMT_S16) {
            auto* out = reinterpret_cast<int16_t*>(frame->data[0]);
            std::memcpy(out, input, data.size());
        } else if (codec->context->sample_fmt == AV_SAMPLE_FMT_S16P) {
            for (int channel = 0; channel < channels; channel++) {
                auto* out = reinterpret_cast<int16_t*>(frame->data[channel]);
                for (int i = 0; i < samples; i++) {
                    out[i] = input[i * channels + channel];
                }
            }
        }
    } else {
        const int width = codec->width;
        const int height = codec->height;
        const size_t lumaSize = static_cast<size_t>(width) * height;
        const size_t chromaSize = lumaSize / 4;
        if (width <= 0 || height <= 0 || data.size() < lumaSize + chromaSize * 2) {
            av_frame_free(&frame);
            return;
        }
        frame->width = width;
        frame->height = height;
        frame->format = AV_PIX_FMT_YUV420P;
        if (av_frame_get_buffer(frame, 0) < 0) {
            av_frame_free(&frame);
            return;
        }
        const uint8_t* src = data.data();
        for (int y = 0; y < height; y++) {
            std::memcpy(frame->data[0] + static_cast<size_t>(y) * frame->linesize[0],
                        src + static_cast<size_t>(y) * width, width);
        }
        const uint8_t* uSrc = src + lumaSize;
        const uint8_t* vSrc = uSrc + chromaSize;
        for (int y = 0; y < height / 2; y++) {
            std::memcpy(frame->data[1] + static_cast<size_t>(y) * frame->linesize[1],
                        uSrc + static_cast<size_t>(y) * (width / 2), width / 2);
            std::memcpy(frame->data[2] + static_cast<size_t>(y) * frame->linesize[2],
                        vSrc + static_cast<size_t>(y) * (width / 2), width / 2);
        }
    }
    int send = avcodec_send_frame(codec->context, frame);
    av_frame_free(&frame);
    if (send < 0) {
        ALOGE("encode send failed=%d", send);
        return;
    }
    drainEncoder(env, codec);
}

bool configureEncoder(Codec* codec, const std::string& mime, int width, int height,
                      int sampleRate, int channels, int bitrate, int frameRate) {
    const AVCodec* encoder = nullptr;
    if (mime == "video/avc") {
        encoder = avcodec_find_encoder_by_name("libx264");
        if (encoder == nullptr) {
            encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
    } else if (mime == "video/hevc") {
        encoder = avcodec_find_encoder_by_name("libx265");
        if (encoder == nullptr) {
            encoder = avcodec_find_encoder(AV_CODEC_ID_HEVC);
        }
    } else if (mime == "audio/mp4a-latm") {
        encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    } else if (mime == "audio/opus") {
        encoder = avcodec_find_encoder_by_name("libopus");
        if (encoder == nullptr) {
            encoder = avcodec_find_encoder(AV_CODEC_ID_OPUS);
        }
    }
    if (encoder == nullptr) {
        ALOGE("no ffmpeg encoder for %s", mime.c_str());
        return false;
    }
    AVCodecContext* context = avcodec_alloc_context3(encoder);
    if (context == nullptr) {
        return false;
    }
    context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    context->thread_count = 1;
    if (encoder->type == AVMEDIA_TYPE_VIDEO) {
        context->width = std::max(2, width & ~1);
        context->height = std::max(2, height & ~1);
        context->pix_fmt = AV_PIX_FMT_YUV420P;
        context->time_base = AVRational{1, std::max(1, frameRate)};
        context->framerate = AVRational{std::max(1, frameRate), 1};
        context->bit_rate = bitrate > 0 ? bitrate : 1000000;
        context->gop_size = std::max(1, frameRate);
        context->max_b_frames = 0;
    } else {
        context->sample_rate = sampleRate > 0 ? sampleRate : 44100;
        context->ch_layout = AVChannelLayout{};
        av_channel_layout_default(&context->ch_layout, std::max(1, channels));
        context->sample_fmt = encoder->sample_fmts != nullptr
                ? encoder->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        context->bit_rate = bitrate > 0 ? bitrate : 128000;
        context->time_base = AVRational{1, context->sample_rate};
    }
    if (avcodec_open2(context, encoder, nullptr) < 0) {
        avcodec_free_context(&context);
        return false;
    }
    if (codec->context != nullptr) {
        avcodec_free_context(&codec->context);
    }
    codec->context = context;
    codec->decoder = encoder;
    codec->width = context->width;
    codec->height = context->height;
    codec->sampleRate = context->sample_rate;
    codec->channels = context->ch_layout.nb_channels;
    codec->extradata.clear();
    if (context->extradata != nullptr && context->extradata_size > 0) {
        codec->extradata.assign(context->extradata,
                                context->extradata + context->extradata_size);
    }
    return true;
}

// ---------------------------------------------------------------------------
// JNI methods
// ---------------------------------------------------------------------------

void nativeInit(JNIEnv*, jclass) {}

void nativeSetup(JNIEnv* env, jobject thiz, jstring name, jboolean nameIsType,
                 jboolean encoder, jint /*pid*/, jint /*uid*/) {
    auto* codec = new Codec();
    env->GetJavaVM(&codec->vm);
    codec->weakSelf = env->NewGlobalRef(thiz);
    jclass cls = env->GetObjectClass(thiz);
    codec->postEvent = env->GetMethodID(cls, "postEventFromNative",
                                        "(IIILjava/lang/Object;)V");
    const char* raw = env->GetStringUTFChars(name, nullptr);
    std::string nameText = raw != nullptr ? raw : "";
    if (raw != nullptr) {
        env->ReleaseStringUTFChars(name, raw);
    }
    if (nameIsType) {
        codec->mime = nameText;
        const HostCodecInfo* info = hostCodecForMime(nameText.c_str());
        codec->codecName = info != nullptr ? info->name : "droidlet.ffmpeg.decoder";
    } else {
        const char* mime = hostMimeForCodecName(nameText.c_str());
        codec->mime = mime != nullptr ? mime : "";
        codec->codecName = nameText;
    }
    codec->encoder = encoder == JNI_TRUE;
    allocateBuffers(env, codec);
    setCodec(env, thiz, codec);
}

void readByteBuffer(JNIEnv* env, jobject value, std::vector<uint8_t>& out) {
    if (value == nullptr) {
        return;
    }
    jclass bufferClass = env->GetObjectClass(value);
    jmethodID positionMethod = env->GetMethodID(bufferClass, "position", "()I");
    jmethodID limitMethod = env->GetMethodID(bufferClass, "limit", "()I");
    jint position = 0;
    jint limit = 0;
    if (positionMethod != nullptr && limitMethod != nullptr) {
        position = env->CallIntMethod(value, positionMethod);
        limit = env->CallIntMethod(value, limitMethod);
    }
    if (limit < position) {
        limit = position;
    }
    const size_t size = static_cast<size_t>(limit - position);

    auto* address = static_cast<uint8_t*>(env->GetDirectBufferAddress(value));
    jlong capacity = env->GetDirectBufferCapacity(value);
    if (address != nullptr && capacity >= limit && size > 0) {
        out.assign(address + position, address + limit);
        return;
    }
    // MediaFormat commonly holds a heap ByteBuffer (wrap()).
    jmethodID arrayMethod = env->GetMethodID(bufferClass, "array", "()[B");
    jmethodID offsetMethod = env->GetMethodID(bufferClass, "arrayOffset", "()I");
    if (arrayMethod == nullptr || offsetMethod == nullptr) {
        env->ExceptionClear();
        return;
    }
    auto array = static_cast<jbyteArray>(env->CallObjectMethod(value, arrayMethod));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    if (array == nullptr) {
        return;
    }
    jint arrayOffset = env->CallIntMethod(value, offsetMethod);
    jsize length = env->GetArrayLength(array);
    if (arrayOffset + position + static_cast<jint>(size) > length) {
        env->DeleteLocalRef(array);
        return;
    }
    jbyte* bytes = env->GetByteArrayElements(array, nullptr);
    if (bytes != nullptr && size > 0) {
        out.assign(reinterpret_cast<uint8_t*>(bytes) + arrayOffset + position,
                   reinterpret_cast<uint8_t*>(bytes) + arrayOffset + position + size);
    }
    if (bytes != nullptr) {
        env->ReleaseByteArrayElements(array, bytes, JNI_ABORT);
    }
    env->DeleteLocalRef(array);
}

void nativeConfigure(JNIEnv* env, jobject thiz, jobjectArray keys, jobjectArray values,
                     jobject surface, jobject /*crypto*/, jobject /*descrambler*/,
                     jint /*flags*/) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr) {
        return;
    }
    std::string mime;
    int width = 0;
    int height = 0;
    int sampleRate = 0;
    int channels = 0;
    int bitrate = 0;
    int frameRate = 30;
    int colorFormat = 0;
    std::vector<uint8_t> csd;
    std::vector<uint8_t> csd1;
    std::vector<uint8_t> csd2;
    if (keys != nullptr && values != nullptr) {
        jsize count = env->GetArrayLength(keys);
        for (jsize i = 0; i < count; i++) {
            auto key = static_cast<jstring>(env->GetObjectArrayElement(keys, i));
            jobject value = env->GetObjectArrayElement(values, i);
            const char* keyText = env->GetStringUTFChars(key, nullptr);
            std::string keyString = keyText != nullptr ? keyText : "";
            if (keyText != nullptr) {
                env->ReleaseStringUTFChars(key, keyText);
            }
            if (keyString == "mime" && value != nullptr) {
                auto string = static_cast<jstring>(value);
                const char* text = env->GetStringUTFChars(string, nullptr);
                mime = text != nullptr ? text : "";
                if (text != nullptr) {
                    env->ReleaseStringUTFChars(string, text);
                }
            } else if (keyString == "width" && value != nullptr) {
                jclass cls = env->GetObjectClass(value);
                jmethodID intValue = env->GetMethodID(cls, "intValue", "()I");
                width = env->CallIntMethod(value, intValue);
            } else if (keyString == "height" && value != nullptr) {
                jclass cls = env->GetObjectClass(value);
                jmethodID intValue = env->GetMethodID(cls, "intValue", "()I");
                height = env->CallIntMethod(value, intValue);
            } else if (keyString == "sample-rate" && value != nullptr) {
                jclass cls = env->GetObjectClass(value);
                jmethodID intValue = env->GetMethodID(cls, "intValue", "()I");
                sampleRate = env->CallIntMethod(value, intValue);
            } else if (keyString == "channel-count" && value != nullptr) {
                jclass cls = env->GetObjectClass(value);
                jmethodID intValue = env->GetMethodID(cls, "intValue", "()I");
                channels = env->CallIntMethod(value, intValue);
            } else if ((keyString == "bitrate" || keyString == "bit-rate") &&
                       value != nullptr) {
                jclass cls = env->GetObjectClass(value);
                jmethodID intValue = env->GetMethodID(cls, "intValue", "()I");
                bitrate = env->CallIntMethod(value, intValue);
            } else if (keyString == "frame-rate" && value != nullptr) {
                jclass cls = env->GetObjectClass(value);
                jmethodID intValue = env->GetMethodID(cls, "intValue", "()I");
                frameRate = env->CallIntMethod(value, intValue);
            } else if (keyString == "color-format" && value != nullptr) {
                jclass cls = env->GetObjectClass(value);
                jmethodID intValue = env->GetMethodID(cls, "intValue", "()I");
                colorFormat = env->CallIntMethod(value, intValue);
            } else if (keyString == "csd-0") {
                readByteBuffer(env, value, csd);
            } else if (keyString == "csd-1") {
                readByteBuffer(env, value, csd1);
            } else if (keyString == "csd-2") {
                readByteBuffer(env, value, csd2);
            }
            if (value != nullptr) {
                env->DeleteLocalRef(value);
            }
            env->DeleteLocalRef(key);
        }
    }
    if (mime.empty()) {
        mime = codec->mime;
    }
    AVCodecID codecId = codecIdForMime(mime);
    const AVCodec* decoder = avcodec_find_decoder(codecId);
    if (decoder == nullptr) {
        ALOGE("no ffmpeg decoder for %s", mime.c_str());
        return;
    }
    codec->mime = mime;
    if (codec->codecName.empty()) {
        const HostCodecInfo* info = hostCodecForMime(mime.c_str());
        codec->codecName = info != nullptr ? info->name : "droidlet.ffmpeg.decoder";
    }
    codec->mime = mime;
    codec->colorFormat = colorFormat;
    codec->frameRate = frameRate;
    if (codec->encoder) {
        if (!configureEncoder(codec, mime, width, height, sampleRate, channels, bitrate,
                              frameRate)) {
            return;
        }
        clearPending(codec);
        codec->async = codec->callback != nullptr;
        codec->formatChangeSent = false;
        codec->codecConfigQueued = false;
        codec->eosQueued = false;
        codec->eosOutputSent = false;
        ALOGI("configured encoder %s %dx%d rate=%d ch=%d bitrate=%d", mime.c_str(),
                      codec->width, codec->height, codec->sampleRate, codec->channels,
                      bitrate);
        return;
    }
    if (codec->context != nullptr) {
        avcodec_free_context(&codec->context);
    }
    if (mime == "video/avc") {
        if (looksAnnexB(csd)) {
            // Some extractors split SPS and PPS into csd-0 and csd-1, each in
            // Annex B form.
            if (looksAnnexB(csd1)) {
                csd.insert(csd.end(), csd1.begin(), csd1.end());
            }
            if (looksAnnexB(csd2)) {
                csd.insert(csd.end(), csd2.begin(), csd2.end());
            }
            std::vector<uint8_t> avcc = annexBToAvcC(csd);
            if (!avcc.empty()) {
                csd = std::move(avcc);
            }
        }
    } else if (mime == "video/hevc" && looksAnnexB(csd)) {
        // HEVC parameter sets are VPS/SPS/PPS and may be split across csd-0,
        // csd-1 and csd-2.  ffmpeg accepts them in-band, so prepend them to
        // the first sample instead of building an hvcC record.
        if (looksAnnexB(csd1)) {
            csd.insert(csd.end(), csd1.begin(), csd1.end());
        }
        if (looksAnnexB(csd2)) {
            csd.insert(csd.end(), csd2.begin(), csd2.end());
        }
        codec->parameterSets = csd;
        csd.clear();
    }
    codec->context = avcodec_alloc_context3(decoder);
    if (codec->context == nullptr) {
        return;
    }
    codec->context->thread_count = 1;
    if (!csd.empty()) {
        codec->context->extradata = static_cast<uint8_t*>(
                av_mallocz(csd.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        codec->context->extradata_size = static_cast<int>(csd.size());
        std::memcpy(codec->context->extradata, csd.data(), csd.size());
    }
    if (avcodec_open2(codec->context, decoder, nullptr) < 0) {
        avcodec_free_context(&codec->context);
        return;
    }
    codec->decoder = decoder;
    codec->extradata = csd;
    codec->extradataChecked = !csd.empty();
    if (initBitstreamFilter(codec)) {
        // Bitstream filter is ready for MP4 samples.
    }
    codec->width = width > 0 ? width : codec->context->width;
    codec->height = height > 0 ? height : codec->context->height;
    codec->sampleRate = sampleRate > 0 ? sampleRate : codec->context->sample_rate;
    codec->channels = channels > 0 ? channels : codec->context->ch_layout.nb_channels;
    if (codec->context->codec_type == AVMEDIA_TYPE_AUDIO && codec->channels > 0) {
        AVChannelLayout inputLayout = codec->context->ch_layout;
        if (inputLayout.nb_channels == 0) {
            av_channel_layout_default(&inputLayout, codec->channels);
        } else if (inputLayout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&inputLayout, inputLayout.nb_channels);
        }
        AVChannelLayout outputLayout{};
        av_channel_layout_default(&outputLayout, codec->channels);
        const int inputRate = codec->context->sample_rate > 0
                ? codec->context->sample_rate : codec->sampleRate;
        int swrResult = swr_alloc_set_opts2(&codec->resampler, &outputLayout, AV_SAMPLE_FMT_S16,
                                            codec->sampleRate, &inputLayout,
                                            codec->context->sample_fmt,
                                            inputRate, 0, nullptr);
        if (codec->resampler != nullptr) {
            swr_init(codec->resampler);
        } else {
            ALOGW("resampler allocation failed=%d", swrResult);
        }
    }
    if (surface != nullptr) {
        FromSurfaceFn fromSurface = fromSurfaceFunction();
        if (fromSurface != nullptr) {
            codec->window = fromSurface(env, surface);
        }
    }
    clearPending(codec);
    codec->async = codec->callback != nullptr;
    codec->formatChangeSent = false;
    codec->parameterSetsSent = false;
    codec->eosQueued = false;
    codec->eosOutputSent = false;
    ALOGI("configured %s %dx%d rate=%d ch=%d csd=%zu surface=%d async=%d",
                  codec->mime.c_str(), codec->width, codec->height, codec->sampleRate,
                  codec->channels, csd.size(), codec->window != nullptr, codec->async);
}

void nativeStart(JNIEnv* env, jobject thiz) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr) {
        return;
    }
    codec->started = true;
    if (codec->encoder) {
        queueCodecConfig(env, codec);
    }
    if (codec->async) {
        if (!codec->formatChangeSent) {
            codec->formatChangeSent = true;
            postFormatChange(env, codec);
        }
        if (codec->encoder) {
            postReadyOutputs(env, codec);
        }
        for (int i = 0; i < kBufferCount; i++) {
            if (!codec->inputBusy[i]) {
                postInputAvailable(env, codec, i);
            }
        }
    }
}

void nativeStop(JNIEnv* env, jobject thiz) {
    Codec* codec = getCodec(env, thiz);
    if (codec != nullptr) {
        codec->started = false;
        clearPending(codec);
    }
}

void nativeFlush(JNIEnv* env, jobject thiz) {
    Codec* codec = getCodec(env, thiz);
    if (codec != nullptr) {
        if (codec->context != nullptr) {
            avcodec_flush_buffers(codec->context);
        }
        clearPending(codec);
        codec->started = false;
        codec->formatChangeSent = false;
        codec->parameterSetsSent = false;
        codec->eosQueued = false;
        codec->eosOutputSent = false;
    }
}

void nativeReset(JNIEnv* env, jobject thiz) {
    Codec* codec = getCodec(env, thiz);
    if (codec != nullptr) {
        closeCodec(codec);
        setCodec(env, thiz, nullptr);
    }
}

void nativeRelease(JNIEnv* env, jobject thiz) {
    nativeReset(env, thiz);
}

void nativeFinalize(JNIEnv* env, jobject thiz) {
    nativeRelease(env, thiz);
}

jint dequeueInputBuffer(JNIEnv* env, jobject thiz, jlong /*timeoutUs*/) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr || codec->freeInputs.empty()) {
        return kInfoTryAgainLater;
    }
    int index = codec->freeInputs.front();
    codec->freeInputs.pop_front();
    codec->inputBusy[index] = true;
    return index;
}

void queueInputBuffer(JNIEnv* env, jobject thiz, jint index, jint offset, jint size,
                      jlong ptsUs, jint flags) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr || index < 0 || index >= kBufferCount) {
        return;
    }
    auto* address = static_cast<uint8_t*>(env->GetDirectBufferAddress(
            codec->inputBuffers[index]));
    std::vector<uint8_t> data;
    if (address != nullptr && size > 0) {
        data.assign(address + offset, address + offset + size);
    }
    codec->inputBusy[index] = false;
    codec->freeInputs.push_back(index);
    const bool endOfStream = (flags & kBufferFlagEndOfStream) != 0;
    if (codec->encoder) {
        if (!data.empty()) {
            encodeFrame(env, codec, data, ptsUs);
        } else if (endOfStream && codec->context != nullptr) {
            avcodec_send_frame(codec->context, nullptr);
            drainEncoder(env, codec);
            codec->eosQueued = true;
        }
    } else if (!data.empty()) {
        decodeInput(env, codec, data, ptsUs, flags);
    } else if (endOfStream) {
        codec->eosQueued = true;
    }
    if (codec->async) {
        postInputAvailable(env, codec, index);
        postReadyOutputs(env, codec);
        if (endOfStream) {
            if (!codec->encoder) {
                flushDecoder(env, codec);
                postReadyOutputs(env, codec);
            }
            if (!codec->eosOutputSent) {
                codec->eosOutputSent = true;
                codec->eosQueued = false;
                postOutputAvailable(env, codec, 0, 0, 0, kBufferFlagEndOfStream);
            }
        }
    }
}

jint dequeueOutputBuffer(JNIEnv* env, jobject thiz, jobject info, jlong /*timeoutUs*/) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr || info == nullptr || codec->async) {
        return kInfoTryAgainLater;
    }
    if (!codec->formatChangeSent) {
        codec->formatChangeSent = true;
        return kInfoOutputFormatChanged;
    }
    if (codec->readyOutputs.empty() && codec->eosQueued) {
        // Drain delayed frames before reporting end-of-stream.
        flushDecoder(env, codec);
    }
    if (!codec->readyOutputs.empty()) {
        int index = codec->readyOutputs.front();
        codec->readyOutputs.pop_front();
        if (codec->pendingFrames.count(index) > 0) {
            fillBufferInfo(env, info, 0, 0, codec->pendingFrames[index].ptsUs, 0);
        } else if (codec->pendingAudio.count(index) > 0) {
            auto& audio = codec->pendingAudio[index];
            int64_t pts = audio.second;
            fillBufferInfo(env, info, 0, audio.first, pts, 0);
        } else if (codec->pendingEncoded.count(index) > 0) {
            auto& encoded = codec->pendingEncoded[index];
            int flags = codec->pendingEncodedFlags.count(index) > 0
                    ? codec->pendingEncodedFlags[index] : 0;
            fillBufferInfo(env, info, 0, encoded.first, encoded.second, flags);
        }
        codec->dequeuedOutputs.insert(index);
        return index;
    }
    if (codec->eosQueued && !codec->eosOutputSent) {
        codec->eosOutputSent = true;
        codec->eosQueued = false;
        fillBufferInfo(env, info, 0, 0, 0, kBufferFlagEndOfStream);
        return 0;
    }
    return kInfoTryAgainLater;
}

void releaseOutputBuffer(JNIEnv* env, jobject thiz, jint index, jboolean render,
                         jboolean /*updatePts*/, jlong /*timeNs*/) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr) {
        return;
    }
    auto frame = codec->pendingFrames.find(index);
    if (frame != codec->pendingFrames.end()) {
        if (render && codec->window != nullptr) {
            presentFrame(codec, frame->second.frame);
        }
        av_frame_free(&frame->second.frame);
        codec->pendingFrames.erase(frame);
    }
    codec->pendingAudio.erase(index);
    codec->pendingEncoded.erase(index);
    codec->pendingEncodedFlags.erase(index);
    if (codec->dequeuedOutputs.erase(index) > 0) {
        codec->freeOutputs.push_back(index);
    }
}

jobject getBuffer(JNIEnv* env, jobject thiz, jboolean input, jint index) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr || index < 0 || index >= kBufferCount) {
        return nullptr;
    }
    jobject buffer = input ? codec->inputBuffers[index] : codec->outputBuffers[index];
    if (buffer == nullptr) {
        return nullptr;
    }
    // The buffer must stay the same object across dequeues: clients such as
    // ExoPlayer's AudioSink compare buffer identity.  It may have been marked
    // inaccessible by MediaCodec's legacy getInputBuffers()/getOutputBuffers()
    // caching, so make it accessible again.
    jclass bufferClass = env->GetObjectClass(buffer);
    jmethodID setAccessible = env->GetMethodID(bufferClass, "setAccessible", "(Z)V");
    if (setAccessible != nullptr) {
        env->CallVoidMethod(buffer, setAccessible, JNI_TRUE);
    }
    return buffer;
}

jobjectArray getBuffers(JNIEnv* env, jobject thiz, jboolean input) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr) {
        return nullptr;
    }
    return makeBufferArray(env, input ? codec->inputBuffers : codec->outputBuffers);
}

jobject getFormat(JNIEnv* env, jobject thiz, jboolean input) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr) {
        return nullptr;
    }
    return makeFormatMap(env, codec, input);
}

jobject getOutputFormat(JNIEnv* env, jobject thiz, jint /*index*/) {
    return getFormat(env, thiz, JNI_FALSE);
}

void setCallback(JNIEnv* env, jobject thiz, jobject cb) {
    Codec* codec = getCodec(env, thiz);
    if (codec == nullptr) {
        return;
    }
    if (codec->callback != nullptr) {
        env->DeleteGlobalRef(codec->callback);
        codec->callback = nullptr;
    }
    codec->callback = cb != nullptr ? env->NewGlobalRef(cb) : nullptr;
    codec->async = codec->callback != nullptr;
}
void setSurface(JNIEnv*, jobject, jobject) {}
void detachOutputSurface(JNIEnv*, jobject) {}
void noOpVoid(JNIEnv*, jobject) {}
void noOpVoidInt(JNIEnv*, jobject, jint) {}
void noOpVoidBoolean(JNIEnv*, jobject, jboolean) {}
void noOpVoidObject(JNIEnv*, jobject, jobject) {}
jobject nullObject(JNIEnv*, jobject) { return nullptr; }
jobject nullObjectInt(JNIEnv*, jobject, jint) { return nullptr; }
jobject nullObjectBoolean(JNIEnv*, jobject, jboolean) { return nullptr; }
jobjectArray nullBuffers(JNIEnv*, jobject) { return nullptr; }
jint zeroInt(JNIEnv*, jobject) { return 0; }
jint zeroIntInt(JNIEnv*, jobject, jint) { return 0; }
jint zeroIntObject(JNIEnv*, jobject, jobject) { return 0; }
jobject nullMetrics(JNIEnv*, jobject) { return nullptr; }
jobjectArray nullStringArray(JNIEnv*, jobject) { return nullptr; }
jobject nullList(JNIEnv*, jobject) { return nullptr; }
jobject nullParameterDescriptor(JNIEnv*, jobject, jstring) { return nullptr; }
void noOpVoidStringArray(JNIEnv*, jobject, jobjectArray) {}
void noOpVoidTwoInts(JNIEnv*, jobject, jint, jint) {}
void noOpVoidBooleanInt(JNIEnv*, jobject, jboolean, jint) {}
void signalEndOfInputStream(JNIEnv* env, jobject thiz) {
    queueInputBuffer(env, thiz, 0, 0, 0, 0, kBufferFlagEndOfStream);
}
jobject getOwnCodecInfo(JNIEnv* env, jobject) {
    // Return null; apps that only need decode do not require codec info.
    (void)env;
    return nullptr;
}
void releasePersistentInputSurface(JNIEnv* env, jclass, jobject surface) {
    (void)env;
    (void)surface;
}
jobject createPersistentInputSurface(JNIEnv*, jclass) { return nullptr; }
void setInputSurface(JNIEnv*, jobject, jobject) {}
jboolean falseBoolean(JNIEnv*, jobject) { return JNI_FALSE; }
jboolean falseBooleanInt(JNIEnv*, jobject, jint) { return JNI_FALSE; }
jint zeroIntTwoInts(JNIEnv*, jobject, jint, jint) { return 0; }
jint zeroIntByteArrayByteArray(JNIEnv*, jobject, jbyteArray, jbyteArray) { return 0; }
void noOpVoidIntBoolean(JNIEnv*, jobject, jint, jboolean) {}
void noOpVoidBooleanBoolean(JNIEnv*, jobject, jboolean, jboolean) {}
void noOpVoidTwoObjects(JNIEnv*, jobject, jobjectArray, jobjectArray) {}
void noOpVoidObjectInt(JNIEnv*, jobject, jobject, jint) {}
void noOpVoidIntObjectArray(JNIEnv*, jobject, jint, jobjectArray) {}
void noOpVoidIntIntCryptoLongInt(JNIEnv*, jobject, jint, jint, jobject, jlong, jint) {}
jstring getCanonicalName(JNIEnv* env, jobject thiz) {
    Codec* codec = getCodec(env, thiz);
    const char* name = codec != nullptr && !codec->codecName.empty()
            ? codec->codecName.c_str() : "droidlet.ffmpeg.decoder";
    return env->NewStringUTF(name);
}

const JNINativeMethod kMethods[] = {
        {"native_init", "()V", reinterpret_cast<void*>(nativeInit)},
        {"native_setup", "(Ljava/lang/String;ZZII)V", reinterpret_cast<void*>(nativeSetup)},
        {"native_configure",
         "([Ljava/lang/String;[Ljava/lang/Object;Landroid/view/Surface;"
         "Landroid/media/MediaCrypto;Landroid/os/IHwBinder;I)V",
         reinterpret_cast<void*>(nativeConfigure)},
        {"native_start", "()V", reinterpret_cast<void*>(nativeStart)},
        {"native_stop", "()V", reinterpret_cast<void*>(nativeStop)},
        {"native_flush", "()V", reinterpret_cast<void*>(nativeFlush)},
        {"native_reset", "()V", reinterpret_cast<void*>(nativeReset)},
        {"native_release", "()V", reinterpret_cast<void*>(nativeRelease)},
        {"native_finalize", "()V", reinterpret_cast<void*>(nativeFinalize)},
        {"native_dequeueInputBuffer", "(J)I", reinterpret_cast<void*>(dequeueInputBuffer)},
        {"native_queueInputBuffer", "(IIIJI)V", reinterpret_cast<void*>(queueInputBuffer)},
        {"native_dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I",
         reinterpret_cast<void*>(dequeueOutputBuffer)},
        {"releaseOutputBuffer", "(IZZJ)V", reinterpret_cast<void*>(releaseOutputBuffer)},
        {"getBuffer", "(ZI)Ljava/nio/ByteBuffer;", reinterpret_cast<void*>(getBuffer)},
        {"getBuffers", "(Z)[Ljava/nio/ByteBuffer;", reinterpret_cast<void*>(getBuffers)},
        {"getFormatNative", "(Z)Ljava/util/Map;", reinterpret_cast<void*>(getFormat)},
        {"getOutputFormatNative", "(I)Ljava/util/Map;",
         reinterpret_cast<void*>(getOutputFormat)},
        {"native_setCallback", "(Landroid/media/MediaCodec$Callback;)V",
         reinterpret_cast<void*>(setCallback)},
        {"native_setSurface", "(Landroid/view/Surface;)V", reinterpret_cast<void*>(setSurface)},
        {"native_detachOutputSurface", "()V", reinterpret_cast<void*>(detachOutputSurface)},
        {"setParameters", "([Ljava/lang/String;[Ljava/lang/Object;)V",
         reinterpret_cast<void*>(noOpVoidTwoObjects)},
        {"native_getMetrics", "()Landroid/os/PersistableBundle;",
         reinterpret_cast<void*>(nullMetrics)},
        {"getOwnCodecInfo", "()Landroid/media/MediaCodecInfo;",
         reinterpret_cast<void*>(getOwnCodecInfo)},
        {"native_getGloballyAvailableResources", "()Ljava/util/List;",
         reinterpret_cast<void*>(nullList)},
        {"native_getRequiredResources", "()Ljava/util/List;",
         reinterpret_cast<void*>(nullList)},
        {"createInputSurface", "()Landroid/view/Surface;", reinterpret_cast<void*>(nullObject)},
        {"native_setInputSurface", "(Landroid/view/Surface;)V",
         reinterpret_cast<void*>(setInputSurface)},
        {"native_releasePersistentInputSurface", "(Landroid/view/Surface;)V",
         reinterpret_cast<void*>(releasePersistentInputSurface)},
        {"signalEndOfInputStream", "()V", reinterpret_cast<void*>(signalEndOfInputStream)},
        {"native_getOutputFrame", "(Landroid/media/MediaCodec$OutputFrame;I)V",
         reinterpret_cast<void*>(noOpVoidObjectInt)},
        {"setVideoScalingMode", "(I)V", reinterpret_cast<void*>(noOpVoidInt)},
        {"native_setAudioPresentation", "(II)V", reinterpret_cast<void*>(noOpVoidTwoInts)},
        {"getCanonicalName", "()Ljava/lang/String;",
         reinterpret_cast<void*>(getCanonicalName)},
        {"native_enableOnFirstTunnelFrameReadyListener", "(Z)V",
         reinterpret_cast<void*>(noOpVoidBoolean)},
        {"native_enableOnFrameRenderedListener", "(Z)V",
         reinterpret_cast<void*>(noOpVoidBoolean)},
        {"native_getSupportedVendorParameters", "()Ljava/util/List;",
         reinterpret_cast<void*>(nullList)},
        {"native_getParameterDescriptor",
         "(Ljava/lang/String;)Landroid/media/MediaCodec$ParameterDescriptor;",
         reinterpret_cast<void*>(nullParameterDescriptor)},
        {"native_subscribeToVendorParameters", "(Ljava/util/List;)V",
         reinterpret_cast<void*>(noOpVoidObject)},
        {"native_unsubscribeFromVendorParameters", "(Ljava/util/List;)V",
         reinterpret_cast<void*>(noOpVoidObject)},
        {"native_setAudioPresentation", "(II)V", reinterpret_cast<void*>(noOpVoidTwoInts)},
        {"native_queueInputBuffers", "(I[Ljava/lang/Object;)V",
         reinterpret_cast<void*>(noOpVoidIntObjectArray)},
        {"native_queueSecureInputBuffer", "(IILandroid/media/MediaCodec$CryptoInfo;JI)V",
         reinterpret_cast<void*>(noOpVoidIntIntCryptoLongInt)},
};

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_android_media_MediaCodec_native_1init(JNIEnv* env, jclass clazz) {
    if (gContextField == nullptr) {
        gContextField = env->GetFieldID(clazz, "mNativeContext", "J");
    }
    if (gBufferInfoClass == nullptr) {
        gBufferInfoClass = reinterpret_cast<jclass>(env->NewGlobalRef(
                env->FindClass("android/media/MediaCodec$BufferInfo")));
        gInfoOffset = env->GetFieldID(gBufferInfoClass, "offset", "I");
        gInfoSize = env->GetFieldID(gBufferInfoClass, "size", "I");
        gInfoPts = env->GetFieldID(gBufferInfoClass, "presentationTimeUs", "J");
        gInfoFlags = env->GetFieldID(gBufferInfoClass, "flags", "I");
    }
    const size_t count = sizeof(kMethods) / sizeof(kMethods[0]);
    for (size_t i = 0; i < count; i++) {
        if (env->RegisterNatives(clazz, &kMethods[i], 1) != 0) {
            ALOGE("registration failed for %s %s", kMethods[i].name,
                          kMethods[i].signature);
            env->ExceptionClear();
        }
    }
}
