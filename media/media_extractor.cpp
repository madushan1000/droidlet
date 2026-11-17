/*
 * Host MediaExtractor backend for droidlet.
 *
 * The stock android.media.MediaExtractor class loads "media_jni" and calls
 * native_init().  We implement the demuxing subset with host ffmpeg:
 * setDataSource(), track formats, select/seek/advance/readSampleData and
 * sample metadata.  Codec-specific data is exposed as csd-0 so MediaCodec
 * can be configured from the track format.
 */

#include <jni.h>

#define LOG_TAG "droidlet-media"
#include <log/log.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
}

namespace {

constexpr int kSampleFlagSync = 1;
constexpr int kSampleFlagEncrypted = 2;

struct FdSource {
    int fd = -1;
    int64_t offset = 0;
    int64_t length = 0;
    int64_t position = 0;
};

struct Extractor {
    AVFormatContext* format = nullptr;
    AVIOContext* avio = nullptr;
    uint8_t* avioBuffer = nullptr;
    FdSource* source = nullptr;
    AVPacket* packet = nullptr;
    bool hasPacket = false;
    std::set<int> selected;
};

jfieldID gContextField = nullptr;

int readPacket(void* opaque, uint8_t* buf, int size) {
    auto* source = static_cast<FdSource*>(opaque);
    int64_t remaining = source->length > 0 ? source->length - source->position : INT64_MAX;
    if (remaining <= 0) {
        return AVERROR_EOF;
    }
    int64_t want = std::min<int64_t>(size, remaining);
    ssize_t got = pread(source->fd, buf, static_cast<size_t>(want),
                        source->offset + source->position);
    if (got < 0) {
        return AVERROR(errno);
    }
    if (got == 0) {
        return AVERROR_EOF;
    }
    source->position += got;
    return static_cast<int>(got);
}

int64_t seekPacket(void* opaque, int64_t offset, int whence) {
    auto* source = static_cast<FdSource*>(opaque);
    if (whence & AVSEEK_SIZE) {
        return source->length;
    }
    whence &= ~AVSEEK_FORCE;
    int64_t base = 0;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = source->position;
            break;
        case SEEK_END:
            base = source->length;
            break;
        default:
            return AVERROR(EINVAL);
    }
    int64_t target = base + offset;
    if (target < 0) {
        return AVERROR(EINVAL);
    }
    if (source->length > 0 && target > source->length) {
        target = source->length;
    }
    source->position = target;
    return target;
}

void closeExtractor(Extractor* extractor) {
    if (extractor == nullptr) {
        return;
    }
    if (extractor->packet != nullptr) {
        av_packet_free(&extractor->packet);
    }
    if (extractor->format != nullptr) {
        avformat_close_input(&extractor->format);
    }
    if (extractor->avio != nullptr) {
        av_freep(&extractor->avio->buffer);
        avio_context_free(&extractor->avio);
    } else {
        av_freep(&extractor->avioBuffer);
    }
    if (extractor->source != nullptr) {
        if (extractor->source->fd >= 0) {
            close(extractor->source->fd);
        }
        delete extractor->source;
    }
    delete extractor;
}

Extractor* getExtractor(JNIEnv* env, jobject thiz) {
    return reinterpret_cast<Extractor*>(env->GetLongField(thiz, gContextField));
}

void setExtractor(JNIEnv* env, jobject thiz, Extractor* extractor) {
    env->SetLongField(thiz, gContextField, reinterpret_cast<jlong>(extractor));
}

const char* mimeForCodec(AVCodecID codec, AVMediaType type) {
    switch (codec) {
        case AV_CODEC_ID_H264:
            return "video/avc";
        case AV_CODEC_ID_HEVC:
            return "video/hevc";
        case AV_CODEC_ID_MPEG4:
            return "video/mp4v-es";
        case AV_CODEC_ID_H263:
            return "video/3gpp";
        case AV_CODEC_ID_VP8:
            return "video/x-vnd.on2.vp8";
        case AV_CODEC_ID_VP9:
            return "video/x-vnd.on2.vp9";
        case AV_CODEC_ID_AV1:
            return "video/av01";
        case AV_CODEC_ID_AAC:
            return "audio/mp4a-latm";
        case AV_CODEC_ID_MP3:
            return "audio/mpeg";
        case AV_CODEC_ID_OPUS:
            return "audio/opus";
        case AV_CODEC_ID_VORBIS:
            return "audio/vorbis";
        case AV_CODEC_ID_FLAC:
            return "audio/flac";
        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S16BE:
        case AV_CODEC_ID_PCM_U8:
        case AV_CODEC_ID_PCM_F32LE:
            return "audio/raw";
        default:
            return type == AVMEDIA_TYPE_VIDEO ? "video/unknown" : "audio/unknown";
    }
}

jobject newFormatMap(JNIEnv* env, AVFormatContext* format, int index) {
    AVStream* stream = format->streams[index];
    AVCodecParameters* parameters = stream->codecpar;
    jclass mapClass = env->FindClass("java/util/HashMap");
    jmethodID constructor = env->GetMethodID(mapClass, "<init>", "()V");
    jmethodID put = env->GetMethodID(
            mapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    jobject map = env->NewObject(mapClass, constructor);

    auto putObject = [&](const char* key, jobject value) {
        jstring javaKey = env->NewStringUTF(key);
        env->CallObjectMethod(map, put, javaKey, value);
        env->DeleteLocalRef(javaKey);
        if (value != nullptr) {
            env->DeleteLocalRef(value);
        }
    };
    auto putString = [&](const char* key, const char* value) {
        jstring javaValue = env->NewStringUTF(value);
        putObject(key, javaValue);
    };
    auto putInt = [&](const char* key, int value) {
        jclass integerClass = env->FindClass("java/lang/Integer");
        jmethodID valueOf = env->GetStaticMethodID(
                integerClass, "valueOf", "(I)Ljava/lang/Integer;");
        putObject(key, env->CallStaticObjectMethod(integerClass, valueOf, value));
    };
    auto putLong = [&](const char* key, int64_t value) {
        jclass longClass = env->FindClass("java/lang/Long");
        jmethodID valueOf = env->GetStaticMethodID(
                longClass, "valueOf", "(J)Ljava/lang/Long;");
        putObject(key, env->CallStaticObjectMethod(longClass, valueOf, value));
    };
    auto putBytes = [&](const char* key, const uint8_t* data, int size) {
        jbyteArray array = env->NewByteArray(size);
        env->SetByteArrayRegion(array, 0, size, reinterpret_cast<const jbyte*>(data));
        jclass bufferClass = env->FindClass("java/nio/ByteBuffer");
        jmethodID wrap = env->GetStaticMethodID(
                bufferClass, "wrap", "([B)Ljava/nio/ByteBuffer;");
        jobject buffer = env->CallStaticObjectMethod(bufferClass, wrap, array);
        env->DeleteLocalRef(array);
        putObject(key, buffer);
    };

    putString("mime", mimeForCodec(parameters->codec_id, parameters->codec_type));
    if (format->duration != AV_NOPTS_VALUE) {
        putLong("durationUs", format->duration);
    }
    putInt("max-input-size", 1 << 20);
    if (parameters->bit_rate > 0) {
        putInt("bitrate", static_cast<int>(parameters->bit_rate));
    }
    if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
        putInt("width", parameters->width);
        putInt("height", parameters->height);
        AVRational rate = stream->avg_frame_rate;
        if (rate.num > 0 && rate.den > 0) {
            putInt("frame-rate", rate.num / rate.den);
        }
        int rotation = 0;
        for (int i = 0; i < parameters->nb_coded_side_data; i++) {
            const AVPacketSideData* sideData = &parameters->coded_side_data[i];
            if (sideData->type == AV_PKT_DATA_DISPLAYMATRIX &&
                sideData->size >= 9 * sizeof(int32_t)) {
                // Keep the sign convention simple: ffmpeg reports clockwise
                // rotation as a negative angle for many phone captures.
                const int32_t* matrix = reinterpret_cast<const int32_t*>(sideData->data);
                double degrees = -av_display_rotation_get(matrix);
                if (degrees < 0) {
                    degrees += 360;
                }
                rotation = static_cast<int>(std::lround(degrees));
                break;
            }
        }
        if (rotation != 0) {
            putInt("rotation-degrees", rotation);
        }
    } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
        putInt("sample-rate", parameters->sample_rate);
        putInt("channel-count", parameters->ch_layout.nb_channels);
    }
    if (parameters->extradata != nullptr && parameters->extradata_size > 0) {
        putBytes("csd-0", parameters->extradata, parameters->extradata_size);
    }
    return map;
}

jobject fileFormatMap(JNIEnv* env, AVFormatContext* format) {
    jclass mapClass = env->FindClass("java/util/HashMap");
    jmethodID constructor = env->GetMethodID(mapClass, "<init>", "()V");
    jmethodID put = env->GetMethodID(
            mapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    jobject map = env->NewObject(mapClass, constructor);
    jstring key = env->NewStringUTF("mime");
    jstring value = env->NewStringUTF(format->iformat->name != nullptr
            ? format->iformat->name : "application/octet-stream");
    env->CallObjectMethod(map, put, key, value);
    env->DeleteLocalRef(key);
    env->DeleteLocalRef(value);
    return map;
}

bool openSource(Extractor* extractor, int fd, int64_t offset, int64_t length) {
    int heapFd = dup(fd);
    if (heapFd < 0) {
        return false;
    }
    auto* source = new FdSource();
    source->fd = heapFd;
    source->offset = offset;
    source->length = length;
    extractor->source = source;
    extractor->avioBuffer = static_cast<uint8_t*>(av_malloc(32768));
    if (extractor->avioBuffer == nullptr) {
        return false;
    }
    extractor->avio = avio_alloc_context(extractor->avioBuffer, 32768, 0, source, readPacket,
                                         nullptr, seekPacket);
    extractor->format = avformat_alloc_context();
    if (extractor->avio == nullptr || extractor->format == nullptr) {
        return false;
    }
    extractor->format->pb = extractor->avio;
    extractor->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&extractor->format, nullptr, nullptr, nullptr) < 0) {
        return false;
    }
    avformat_find_stream_info(extractor->format, nullptr);
    extractor->packet = av_packet_alloc();
    return extractor->packet != nullptr;
}

bool openPath(Extractor* extractor, const char* path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    struct stat statBuffer {};
    fstat(fd, &statBuffer);
    bool ok = openSource(extractor, fd, 0, statBuffer.st_size);
    close(fd);
    return ok;
}

void releaseExtractor(JNIEnv* env, jobject thiz) {
    Extractor* extractor = getExtractor(env, thiz);
    setExtractor(env, thiz, nullptr);
    closeExtractor(extractor);
}

// ---------------------------------------------------------------------------
// JNI methods
// ---------------------------------------------------------------------------

void nativeInit(JNIEnv*, jclass) {}

void nativeSetup(JNIEnv* env, jobject thiz) {
    setExtractor(env, thiz, new Extractor());
}

void setDataSourceFd(JNIEnv* env, jobject thiz, jobject fd, jlong offset, jlong length) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr) {
        return;
    }
    closeExtractor(extractor);
    setExtractor(env, thiz, nullptr);
    auto* fresh = new Extractor();
    jclass fdClass = env->FindClass("java/io/FileDescriptor");
    jfieldID descriptorField = env->GetFieldID(fdClass, "descriptor", "I");
    int rawFd = env->GetIntField(fd, descriptorField);
    if (!openSource(fresh, rawFd, offset, length)) {
        closeExtractor(fresh);
        jclass exception = env->FindClass("java/io/IOException");
        env->ThrowNew(exception, "ffmpeg could not open the data source");
        return;
    }
    setExtractor(env, thiz, fresh);
}

void setDataSourcePath(JNIEnv* env, jobject thiz, jobject /*httpServiceBinder*/, jstring path,
                       jobjectArray /*keys*/, jobjectArray /*values*/) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr || path == nullptr) {
        return;
    }
    const char* raw = env->GetStringUTFChars(path, nullptr);
    std::string filename(raw != nullptr ? raw : "");
    if (raw != nullptr) {
        env->ReleaseStringUTFChars(path, raw);
    }
    if (filename.rfind("file://", 0) == 0) {
        filename = filename.substr(7);
    }
    closeExtractor(extractor);
    setExtractor(env, thiz, nullptr);
    auto* fresh = new Extractor();
    if (!openPath(fresh, filename.c_str())) {
        closeExtractor(fresh);
        jclass exception = env->FindClass("java/io/IOException");
        env->ThrowNew(exception, "ffmpeg could not open the data source");
        return;
    }
    setExtractor(env, thiz, fresh);
}

jint getTrackCount(JNIEnv* env, jobject thiz) {
    Extractor* extractor = getExtractor(env, thiz);
    return extractor != nullptr && extractor->format != nullptr
            ? static_cast<jint>(extractor->format->nb_streams) : 0;
}

jobject getTrackFormat(JNIEnv* env, jobject thiz, jint index) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr || extractor->format == nullptr || index < 0 ||
        index >= static_cast<jint>(extractor->format->nb_streams)) {
        return nullptr;
    }
    return newFormatMap(env, extractor->format, index);
}

jobject getFileFormat(JNIEnv* env, jobject thiz) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr || extractor->format == nullptr) {
        return nullptr;
    }
    return fileFormatMap(env, extractor->format);
}

void selectTrack(JNIEnv* env, jobject thiz, jint index) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor != nullptr) {
        extractor->selected.insert(index);
    }
}

void unselectTrack(JNIEnv* env, jobject thiz, jint index) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor != nullptr) {
        extractor->selected.erase(index);
    }
}

void seekTo(JNIEnv* env, jobject thiz, jlong timeUs, jint mode) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr || extractor->format == nullptr) {
        return;
    }
    int streamIndex = -1;
    if (!extractor->selected.empty()) {
        streamIndex = *extractor->selected.begin();
    }
    int64_t target = timeUs;
    if (streamIndex >= 0) {
        target = av_rescale_q(timeUs, AV_TIME_BASE_Q,
                              extractor->format->streams[streamIndex]->time_base);
    }
    int flags = (mode == 0 /* SEEK_TO_PREVIOUS_SYNC */) ? AVSEEK_FLAG_BACKWARD : 0;
    av_seek_frame(extractor->format, streamIndex, target, flags);
    extractor->hasPacket = false;
    if (extractor->packet != nullptr) {
        av_packet_unref(extractor->packet);
    }
}

jboolean advance(JNIEnv* env, jobject thiz) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr || extractor->format == nullptr || extractor->packet == nullptr) {
        return JNI_FALSE;
    }
    if (extractor->hasPacket) {
        av_packet_unref(extractor->packet);
        extractor->hasPacket = false;
    }
    while (av_read_frame(extractor->format, extractor->packet) >= 0) {
        if (extractor->selected.empty() ||
            extractor->selected.count(extractor->packet->stream_index) > 0) {
            extractor->hasPacket = true;
            return JNI_TRUE;
        }
        av_packet_unref(extractor->packet);
    }
    return JNI_FALSE;
}

jint readSampleData(JNIEnv* env, jobject thiz, jobject buffer, jint offset) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr || !extractor->hasPacket || buffer == nullptr) {
        return -1;
    }
    auto* address = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (address == nullptr || capacity < 0) {
        return -1;
    }
    int available = static_cast<int>(capacity) - offset;
    int size = std::min(extractor->packet->size, available);
    if (size <= 0) {
        return 0;
    }
    std::memcpy(address + offset, extractor->packet->data, static_cast<size_t>(size));
    return size;
}

jint getSampleTrackIndex(JNIEnv* env, jobject thiz) {
    Extractor* extractor = getExtractor(env, thiz);
    return extractor != nullptr && extractor->hasPacket ? extractor->packet->stream_index : -1;
}

jlong getSampleTime(JNIEnv* env, jobject thiz) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr || !extractor->hasPacket) {
        return -1;
    }
    AVStream* stream = extractor->format->streams[extractor->packet->stream_index];
    if (extractor->packet->pts == AV_NOPTS_VALUE) {
        return 0;
    }
    return av_rescale_q(extractor->packet->pts, stream->time_base, AV_TIME_BASE_Q);
}

jlong getSampleSize(JNIEnv* env, jobject thiz) {
    Extractor* extractor = getExtractor(env, thiz);
    return extractor != nullptr && extractor->hasPacket ? extractor->packet->size : 0;
}

jint getSampleFlags(JNIEnv* env, jobject thiz) {
    Extractor* extractor = getExtractor(env, thiz);
    if (extractor == nullptr || !extractor->hasPacket) {
        return 0;
    }
    int flags = 0;
    if ((extractor->packet->flags & AV_PKT_FLAG_KEY) != 0) {
        flags |= kSampleFlagSync;
    }
    return flags;
}

jboolean sampleCryptoInfo(JNIEnv*, jobject, jobject) {
    return JNI_FALSE;
}

jlong cachedDuration(JNIEnv*, jobject) { return 0; }
jboolean cacheEndOfStream(JNIEnv*, jobject) { return JNI_FALSE; }
jobject audioPresentations(JNIEnv*, jobject, jint) { return nullptr; }
void noOpVoid(JNIEnv*, jobject) {}
void noOpVoidObject(JNIEnv*, jobject, jobject) {}
void noOpVoidString(JNIEnv*, jobject, jstring) {}
jobject nullMetrics(JNIEnv*, jobject) { return nullptr; }

const JNINativeMethod kMethods[] = {
        {"native_init", "()V", reinterpret_cast<void*>(nativeInit)},
        {"native_setup", "()V", reinterpret_cast<void*>(nativeSetup)},
        {"native_finalize", "()V", reinterpret_cast<void*>(releaseExtractor)},
        {"release", "()V", reinterpret_cast<void*>(releaseExtractor)},
        {"setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
         reinterpret_cast<void*>(setDataSourceFd)},
        {"nativeSetDataSource",
         "(Landroid/os/IBinder;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
         reinterpret_cast<void*>(setDataSourcePath)},
        {"setDataSource", "(Landroid/media/MediaDataSource;)V",
         reinterpret_cast<void*>(noOpVoidObject)},
        {"getTrackCount", "()I", reinterpret_cast<void*>(getTrackCount)},
        {"getTrackFormatNative", "(I)Ljava/util/Map;",
         reinterpret_cast<void*>(getTrackFormat)},
        {"getFileFormatNative", "()Ljava/util/Map;", reinterpret_cast<void*>(getFileFormat)},
        {"selectTrack", "(I)V", reinterpret_cast<void*>(selectTrack)},
        {"unselectTrack", "(I)V", reinterpret_cast<void*>(unselectTrack)},
        {"seekTo", "(JI)V", reinterpret_cast<void*>(seekTo)},
        {"advance", "()Z", reinterpret_cast<void*>(advance)},
        {"readSampleData", "(Ljava/nio/ByteBuffer;I)I",
         reinterpret_cast<void*>(readSampleData)},
        {"getSampleTrackIndex", "()I", reinterpret_cast<void*>(getSampleTrackIndex)},
        {"getSampleTime", "()J", reinterpret_cast<void*>(getSampleTime)},
        {"getSampleSize", "()J", reinterpret_cast<void*>(getSampleSize)},
        {"getSampleFlags", "()I", reinterpret_cast<void*>(getSampleFlags)},
        {"getSampleCryptoInfo", "(Landroid/media/MediaCodec$CryptoInfo;)Z",
         reinterpret_cast<void*>(sampleCryptoInfo)},
        {"getCachedDuration", "()J", reinterpret_cast<void*>(cachedDuration)},
        {"hasCacheReachedEndOfStream", "()Z",
         reinterpret_cast<void*>(cacheEndOfStream)},
        {"native_getAudioPresentations", "(I)Ljava/util/List;",
         reinterpret_cast<void*>(audioPresentations)},
        {"nativeSetMediaCas", "(Landroid/os/IHwBinder;)V",
         reinterpret_cast<void*>(noOpVoidObject)},
        {"native_setLogSessionId", "(Ljava/lang/String;)V",
         reinterpret_cast<void*>(noOpVoidString)},
        {"native_getMetrics", "()Landroid/os/PersistableBundle;",
         reinterpret_cast<void*>(nullMetrics)},
};

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_android_media_MediaExtractor_native_1init(JNIEnv* env, jclass clazz) {
    if (gContextField == nullptr) {
        gContextField = env->GetFieldID(clazz, "mNativeContext", "J");
    }
    if (env->RegisterNatives(clazz, kMethods, sizeof(kMethods) / sizeof(kMethods[0])) != 0) {
        ALOGE("MediaExtractor registration failed");
        env->ExceptionClear();
    }
}
