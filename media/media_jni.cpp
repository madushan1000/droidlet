/*
 * Minimal MediaMetadataRetriever JNI backend for the droidlet host.
 *
 * The stock android.media.MediaMetadataRetriever class loads "media_jni" and
 * calls native_init() from its static initializer.  droidlet does not build
 * the platform media stack, so this library provides that entry point and a
 * small ffmpeg-backed implementation for the methods apps actually use:
 * setDataSource(), extractMetadata() and getFrameAtTime().
 *
 * Playback (MediaExtractor/MediaCodec/MediaPlayer) is not implemented here;
 * those classes will still fail cleanly if an app reaches them.
 */

#include <jni.h>

#include <log/log.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "droidlet-media", __VA_ARGS__)

namespace {

struct FdSource {
    int fd = -1;
    int64_t offset = 0;
    int64_t length = 0;
    int64_t position = 0;
};

struct Retriever {
    AVFormatContext* format = nullptr;
    AVIOContext* avio = nullptr;
    uint8_t* avioBuffer = nullptr;
    FdSource* source = nullptr;
};

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
    int64_t base = 0;
    if (whence & AVSEEK_SIZE) {
        return source->length;
    }
    whence &= ~AVSEEK_FORCE;
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

void closeRetriever(Retriever* retriever) {
    if (retriever == nullptr) {
        return;
    }
    if (retriever->format != nullptr) {
        avformat_close_input(&retriever->format);
    }
    if (retriever->avio != nullptr) {
        av_freep(&retriever->avio->buffer);
        avio_context_free(&retriever->avio);
    } else {
        av_freep(&retriever->avioBuffer);
    }
    if (retriever->source != nullptr) {
        if (retriever->source->fd >= 0) {
            close(retriever->source->fd);
        }
        delete retriever->source;
    }
    delete retriever;
}

Retriever* getRetriever(JNIEnv* env, jobject thiz, jfieldID contextField) {
    auto* retriever = reinterpret_cast<Retriever*>(env->GetLongField(thiz, contextField));
    return retriever;
}

void setRetriever(JNIEnv* env, jobject thiz, jfieldID contextField, Retriever* retriever) {
    env->SetLongField(thiz, contextField, reinterpret_cast<jlong>(retriever));
}

jstring newString(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

std::string number(int64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    return buffer;
}

const char* metadataValue(AVDictionary* metadata, const char* key) {
    AVDictionaryEntry* entry = av_dict_get(metadata, key, nullptr, 0);
    return entry != nullptr ? entry->value : nullptr;
}

int videoStreamIndex(const AVFormatContext* format) {
    for (unsigned i = 0; i < format->nb_streams; i++) {
        if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int audioStreamIndex(const AVFormatContext* format) {
    for (unsigned i = 0; i < format->nb_streams; i++) {
        if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int imageStreamIndex(const AVFormatContext* format) {
    for (unsigned i = 0; i < format->nb_streams; i++) {
        if (format->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int rotationDegrees(const AVStream* stream) {
    for (int i = 0; i < stream->codecpar->nb_coded_side_data; i++) {
        const AVPacketSideData* sideData = &stream->codecpar->coded_side_data[i];
        if (sideData->type == AV_PKT_DATA_DISPLAYMATRIX &&
            sideData->size >= 9 * sizeof(int32_t)) {
            double angle = av_display_rotation_get(
                    reinterpret_cast<const int32_t*>(sideData->data));
            if (angle < 0) {
                angle += 360;
            }
            return static_cast<int>(std::lround(angle));
        }
    }
    AVDictionaryEntry* rotate = av_dict_get(stream->metadata, "rotate", nullptr, 0);
    if (rotate != nullptr) {
        return std::atoi(rotate->value);
    }
    return 0;
}

jobject decodeFrame(JNIEnv* env, Retriever* retriever, int64_t timeUs, int reqWidth,
                    int reqHeight) {
    if (retriever == nullptr || retriever->format == nullptr) {
        return nullptr;
    }
    AVFormatContext* format = retriever->format;
    int streamIndex = videoStreamIndex(format);
    if (streamIndex < 0) {
        return nullptr;
    }
    AVStream* stream = format->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        return nullptr;
    }
    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (codecContext == nullptr) {
        return nullptr;
    }
    if (avcodec_parameters_to_context(codecContext, stream->codecpar) < 0) {
        avcodec_free_context(&codecContext);
        return nullptr;
    }
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        avcodec_free_context(&codecContext);
        return nullptr;
    }

    if (timeUs > 0) {
        int64_t target = av_rescale_q(timeUs, AV_TIME_BASE_Q, stream->time_base);
        av_seek_frame(format, streamIndex, target, AVSEEK_FLAG_BACKWARD);
    } else {
        av_seek_frame(format, streamIndex, 0, AVSEEK_FLAG_BACKWARD);
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* output = nullptr;
    SwsContext* scaler = nullptr;
    bool found = false;
    int decoded = 0;

    while (!found && decoded < 600 && av_read_frame(format, packet) >= 0) {
        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet);
            continue;
        }
        if (avcodec_send_packet(codecContext, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);
        while (avcodec_receive_frame(codecContext, frame) >= 0) {
            decoded++;
            int64_t pts = frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) {
                pts = 0;
            }
            int64_t frameUs = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
            if (frameUs < 0) {
                frameUs = 0;
            }
            if (timeUs <= 0 || frameUs + 1 >= timeUs) {
                output = av_frame_alloc();
                if (output != nullptr && av_frame_ref(output, frame) >= 0) {
                    found = true;
                } else {
                    av_frame_free(&output);
                }
                break;
            }
            av_frame_unref(frame);
        }
    }
    av_packet_free(&packet);
    av_frame_free(&frame);

    jobject bitmap = nullptr;
    if (output != nullptr) {
        int srcWidth = output->width;
        int srcHeight = output->height;
        int dstWidth = reqWidth > 0 ? reqWidth : srcWidth;
        int dstHeight = reqHeight > 0 ? reqHeight : srcHeight;
        // The host app heap is small (often 16 MB).  getFrameAtTime() without
        // an explicit size would otherwise allocate an 8 MB int[] plus an
        // 8 MB Bitmap for a 1080p frame.  Cap the default thumbnail size; an
        // explicit width/height is still honored.
        if (reqWidth <= 0 && reqHeight <= 0) {
            constexpr int kMaxDefaultDimension = 1024;
            if (dstWidth > kMaxDefaultDimension || dstHeight > kMaxDefaultDimension) {
                double scale = static_cast<double>(kMaxDefaultDimension) /
                        std::max(dstWidth, dstHeight);
                dstWidth = std::max(1, static_cast<int>(dstWidth * scale));
                dstHeight = std::max(1, static_cast<int>(dstHeight * scale));
            }
        }
        scaler = sws_getContext(srcWidth, srcHeight, codecContext->pix_fmt,
                                dstWidth, dstHeight, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (scaler != nullptr) {
            std::vector<uint8_t> pixels(static_cast<size_t>(dstWidth) * dstHeight * 4);
            uint8_t* dstData[1] = {pixels.data()};
            int dstLinesize[1] = {dstWidth * 4};
            sws_scale(scaler, output->data, output->linesize, 0, srcHeight,
                      dstData, dstLinesize);

            std::vector<jint> colors(static_cast<size_t>(dstWidth) * dstHeight);
            for (int y = 0; y < dstHeight; y++) {
                const uint8_t* row = pixels.data() + static_cast<size_t>(y) * dstWidth * 4;
                for (int x = 0; x < dstWidth; x++) {
                    uint8_t r = row[x * 4 + 0];
                    uint8_t g = row[x * 4 + 1];
                    uint8_t b = row[x * 4 + 2];
                    uint8_t a = row[x * 4 + 3];
                    colors[static_cast<size_t>(y) * dstWidth + x] =
                            (static_cast<jint>(a) << 24) | (r << 16) | (g << 8) | b;
                }
            }

            jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
            jclass configClass = env->FindClass("android/graphics/Bitmap$Config");
            if (bitmapClass != nullptr && configClass != nullptr) {
                jfieldID argb8888 = env->GetStaticFieldID(
                        configClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
                jobject config = env->GetStaticObjectField(configClass, argb8888);
                jmethodID createBitmap = env->GetStaticMethodID(
                        bitmapClass, "createBitmap",
                        "([IIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
                jintArray array = env->NewIntArray(static_cast<jsize>(colors.size()));
                if (array != nullptr) {
                    env->SetIntArrayRegion(array, 0, static_cast<jsize>(colors.size()),
                                           colors.data());
                    bitmap = env->CallStaticObjectMethod(
                            bitmapClass, createBitmap, array, dstWidth, dstHeight, config);
                }
            }
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            sws_freeContext(scaler);
        }
        av_frame_free(&output);
    }

    avcodec_free_context(&codecContext);
    return bitmap;
}

jobject frameAtTime(JNIEnv* env, jobject thiz, jfieldID contextField, jlong timeUs,
                    jint /*option*/, jint width, jint height) {
    return decodeFrame(env, getRetriever(env, thiz, contextField), timeUs, width, height);
}

// ---------------------------------------------------------------------------
// JNI methods
// ---------------------------------------------------------------------------

jfieldID gContextField = nullptr;

void nativeSetup(JNIEnv* env, jobject thiz) {
    setRetriever(env, thiz, gContextField, nullptr);
}

void nativeInit(JNIEnv*, jclass) {}

void setDataSourceFd(JNIEnv* env, jobject thiz, jobject fd, jlong offset, jlong length) {
    jclass fdClass = env->FindClass("java/io/FileDescriptor");
    jfieldID descriptorField = env->GetFieldID(fdClass, "descriptor", "I");
    int rawFd = env->GetIntField(fd, descriptorField);
    int heapFd = dup(rawFd);
    if (heapFd < 0) {
        jclass exception = env->FindClass("java/lang/IllegalArgumentException");
        env->ThrowNew(exception, "dup failed");
        return;
    }

    auto* source = new FdSource();
    source->fd = heapFd;
    source->offset = offset;
    source->length = length;
    source->position = 0;

    auto* retriever = new Retriever();
    retriever->source = source;
    retriever->avioBuffer = static_cast<uint8_t*>(av_malloc(32768));
    if (retriever->avioBuffer == nullptr) {
        closeRetriever(retriever);
        jclass exception = env->FindClass("java/lang/IllegalArgumentException");
        env->ThrowNew(exception, "out of memory");
        return;
    }
    retriever->avio = avio_alloc_context(retriever->avioBuffer, 32768, 0, source,
                                         readPacket, nullptr, seekPacket);
    if (retriever->avio == nullptr) {
        closeRetriever(retriever);
        jclass exception = env->FindClass("java/lang/IllegalArgumentException");
        env->ThrowNew(exception, "unable to allocate AVIOContext");
        return;
    }
    retriever->format = avformat_alloc_context();
    if (retriever->format == nullptr) {
        closeRetriever(retriever);
        jclass exception = env->FindClass("java/lang/IllegalArgumentException");
        env->ThrowNew(exception, "unable to allocate AVFormatContext");
        return;
    }
    retriever->format->pb = retriever->avio;
    retriever->format->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&retriever->format, nullptr, nullptr, nullptr) < 0) {
        closeRetriever(retriever);
        jclass exception = env->FindClass("java/lang/IllegalArgumentException");
        env->ThrowNew(exception, "ffmpeg could not open the data source");
        return;
    }
    avformat_find_stream_info(retriever->format, nullptr);

    // Replace any previous context.
    Retriever* old = getRetriever(env, thiz, gContextField);
    closeRetriever(old);
    setRetriever(env, thiz, gContextField, retriever);
}

void setDataSourceUnsupported(JNIEnv* env) {
    jclass exception = env->FindClass("java/lang/UnsupportedOperationException");
    env->ThrowNew(exception, "droidlet: streaming MediaDataSource is not supported");
}

jstring extractMetadata(JNIEnv* env, jobject thiz, jint keyCode) {
    Retriever* retriever = getRetriever(env, thiz, gContextField);
    if (retriever == nullptr || retriever->format == nullptr) {
        return nullptr;
    }
    AVFormatContext* format = retriever->format;
    switch (keyCode) {
        case 0:  // METADATA_KEY_CD_TRACK_NUMBER
        case 14: // METADATA_KEY_DISC_NUMBER
        case 21: // METADATA_KEY_TIMED_TEXT_LANGUAGES
        case 22: // METADATA_KEY_IS_DRM
        case 23: // METADATA_KEY_LOCATION
            return nullptr;
        case 1: { // ALBUM
            const char* value = metadataValue(format->metadata, "album");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 2: { // ARTIST
            const char* value = metadataValue(format->metadata, "artist");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 3: { // AUTHOR
            const char* value = metadataValue(format->metadata, "author");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 4: { // COMPOSER
            const char* value = metadataValue(format->metadata, "composer");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 5: { // DATE
            const char* value = metadataValue(format->metadata, "date");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 6: { // GENRE
            const char* value = metadataValue(format->metadata, "genre");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 7: { // TITLE
            const char* value = metadataValue(format->metadata, "title");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 8: { // YEAR
            const char* value = metadataValue(format->metadata, "year");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 9: { // DURATION (ms)
            if (format->duration == AV_NOPTS_VALUE) {
                return nullptr;
            }
            return newString(env, number(format->duration / (AV_TIME_BASE / 1000)));
        }
        case 10: { // NUM_TRACKS
            return newString(env, number(format->nb_streams));
        }
        case 11: { // WRITER
            const char* value = metadataValue(format->metadata, "writer");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 12: { // MIMETYPE
            int video = videoStreamIndex(format);
            if (video >= 0) {
                return newString(env, "video/*");
            }
            if (audioStreamIndex(format) >= 0) {
                return newString(env, "audio/*");
            }
            return nullptr;
        }
        case 13: { // ALBUMARTIST
            const char* value = metadataValue(format->metadata, "album_artist");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 15: { // COMPILATION
            const char* value = metadataValue(format->metadata, "compilation");
            return value != nullptr ? newString(env, value) : nullptr;
        }
        case 16: // HAS_AUDIO
            return newString(env, audioStreamIndex(format) >= 0 ? "yes" : "no");
        case 17: // HAS_VIDEO
            return newString(env, videoStreamIndex(format) >= 0 ? "yes" : "no");
        case 18: { // VIDEO_WIDTH
            int index = videoStreamIndex(format);
            return index >= 0 ? newString(env, number(format->streams[index]->codecpar->width))
                              : nullptr;
        }
        case 19: { // VIDEO_HEIGHT
            int index = videoStreamIndex(format);
            return index >= 0 ? newString(env, number(format->streams[index]->codecpar->height))
                              : nullptr;
        }
        case 20: { // BITRATE
            int64_t bitrate = format->bit_rate;
            if (bitrate <= 0) {
                int index = videoStreamIndex(format);
                if (index >= 0) {
                    bitrate = format->streams[index]->codecpar->bit_rate;
                }
            }
            return bitrate > 0 ? newString(env, number(bitrate)) : nullptr;
        }
        case 24: { // VIDEO_ROTATION
            int index = videoStreamIndex(format);
            return index >= 0 ? newString(env, number(rotationDegrees(format->streams[index])))
                              : nullptr;
        }
        case 25: { // CAPTURE_FRAMERATE
            int index = videoStreamIndex(format);
            if (index < 0) {
                return nullptr;
            }
            AVRational rate = format->streams[index]->avg_frame_rate;
            if (rate.num == 0 || rate.den == 0) {
                return nullptr;
            }
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.3f",
                          static_cast<double>(rate.num) / rate.den);
            return newString(env, buffer);
        }
        default:
            return nullptr;
    }
}

void releaseRetriever(JNIEnv* env, jobject thiz) {
    Retriever* retriever = getRetriever(env, thiz, gContextField);
    setRetriever(env, thiz, gContextField, nullptr);
    closeRetriever(retriever);
}

jbyteArray embeddedPicture(JNIEnv* env, jobject thiz, jint /*pictureType*/) {
    Retriever* retriever = getRetriever(env, thiz, gContextField);
    if (retriever == nullptr || retriever->format == nullptr) {
        return nullptr;
    }
    int index = imageStreamIndex(retriever->format);
    if (index < 0) {
        return nullptr;
    }
    AVStream* stream = retriever->format->streams[index];
    if (stream->attached_pic.data == nullptr || stream->attached_pic.size <= 0) {
        return nullptr;
    }
    jbyteArray array = env->NewByteArray(stream->attached_pic.size);
    if (array != nullptr) {
        env->SetByteArrayRegion(array, 0, stream->attached_pic.size,
                                reinterpret_cast<const jbyte*>(stream->attached_pic.data));
    }
    return array;
}

void setDataSourceMediaDataSource(JNIEnv* env, jobject, jobject) {
    setDataSourceUnsupported(env);
}

void setDataSourceUri(JNIEnv* env, jobject, jobject, jstring, jobjectArray, jobjectArray) {
    setDataSourceUnsupported(env);
}

jobject getFrameAtTimeNative(JNIEnv* env, jobject thiz, jlong timeUs, jint option, jint width,
                             jint height, jobject) {
    return frameAtTime(env, thiz, gContextField, timeUs, option, width, height);
}

jobject getFrameAtIndexNative(JNIEnv*, jobject, jint, jint, jobject) {
    return nullptr;
}

jobject getImageAtIndexNative(JNIEnv*, jobject, jint, jobject) {
    return nullptr;
}

jobject getThumbnailImageAtIndexNative(JNIEnv*, jobject, jint, jobject, jint, jint) {
    return nullptr;
}

JNINativeMethod gMethods[] = {
        {"native_setup", "()V", reinterpret_cast<void*>(nativeSetup)},
        {"native_init", "()V", reinterpret_cast<void*>(nativeInit)},
        {"_setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
         reinterpret_cast<void*>(setDataSourceFd)},
        {"_setDataSource", "(Landroid/media/MediaDataSource;)V",
         reinterpret_cast<void*>(setDataSourceMediaDataSource)},
        {"_setDataSource",
         "(Landroid/os/IBinder;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
         reinterpret_cast<void*>(setDataSourceUri)},
        {"nativeExtractMetadata", "(I)Ljava/lang/String;",
         reinterpret_cast<void*>(extractMetadata)},
        {"_getFrameAtTime",
         "(JIIILandroid/media/MediaMetadataRetriever$BitmapParams;)Landroid/graphics/Bitmap;",
         reinterpret_cast<void*>(getFrameAtTimeNative)},
        {"_getFrameAtIndex",
         "(IILandroid/media/MediaMetadataRetriever$BitmapParams;)Ljava/util/List;",
         reinterpret_cast<void*>(getFrameAtIndexNative)},
        {"_getImageAtIndex",
         "(ILandroid/media/MediaMetadataRetriever$BitmapParams;)Landroid/graphics/Bitmap;",
         reinterpret_cast<void*>(getImageAtIndexNative)},
        {"getThumbnailImageAtIndex",
         "(ILandroid/media/MediaMetadataRetriever$BitmapParams;II)Landroid/graphics/Bitmap;",
         reinterpret_cast<void*>(getThumbnailImageAtIndexNative)},
        {"getEmbeddedPicture", "(I)[B", reinterpret_cast<void*>(embeddedPicture)},
        {"release", "()V", reinterpret_cast<void*>(releaseRetriever)},
        {"native_finalize", "()V", reinterpret_cast<void*>(releaseRetriever)},
};

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_android_media_MediaMetadataRetriever_native_1init(JNIEnv* env, jclass clazz) {
    if (gContextField == nullptr) {
        gContextField = env->GetFieldID(clazz, "mNativeContext", "J");
    }
    env->RegisterNatives(clazz, gMethods, sizeof(gMethods) / sizeof(gMethods[0]));
}
