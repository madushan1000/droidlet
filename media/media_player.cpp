/*
 * Minimal MediaPlayer backend for the droidlet host.
 *
 * The stock android.media.MediaPlayer class is a Java state machine over a
 * large set of native methods.  droidlet supplies the subset needed by simple
 * file/asset playback and implements it with host ffmpeg: demux, decode and
 * present video frames into the SurfaceView's ANativeWindow.  Audio decoding
 * is accepted but not yet routed to an audio sink.
 */

#include <jni.h>

#define LOG_TAG "droidlet-media"
#include <log/log.h>

#include <android/native_window.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "audio_sink.h"

namespace {

// MediaPlayer event codes (must match MediaPlayer.java).
constexpr int kMediaPrepared = 1;
constexpr int kMediaPlaybackComplete = 2;
constexpr int kMediaSeekComplete = 4;
constexpr int kMediaSetVideoSize = 5;
constexpr int kMediaStarted = 6;
constexpr int kMediaPaused = 7;
constexpr int kMediaStopped = 8;
constexpr int kMediaError = 100;
constexpr int kMediaErrorUnknown = 1;
constexpr int kMediaErrorIo = -1004;
constexpr int kMediaErrorUnsupported = -1010;

struct FdSource {
    int fd = -1;
    int64_t offset = 0;
    int64_t length = 0;
    int64_t position = 0;
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

struct PlayerContext {
    JavaVM* vm = nullptr;
    jobject weakRef = nullptr;
    jclass mediaPlayerClass = nullptr;
    jmethodID postEvent = nullptr;

    AVFormatContext* format = nullptr;
    AVIOContext* avio = nullptr;
    uint8_t* avioBuffer = nullptr;
    FdSource* source = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    AVBufferRef* hwDevice = nullptr;
    bool hardwareDecode = false;
    SwrContext* resampler = nullptr;
    HostAudioSink sink;
    int videoStream = -1;
    int audioStream = -1;

    ANativeWindow* window = nullptr;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable condition;
    bool workerStarted = false;
    bool releaseRequested = false;
    bool playing = false;
    bool prepared = false;
    bool seekRequested = false;
    bool stopRequested = false;
    bool looping = false;
    bool completed = false;
    int64_t seekTargetMs = 0;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    int audioSessionId = 1;
    float leftVolume = 1.0f;
    float rightVolume = 1.0f;

    bool clockValid = false;

    // Audio decoding and playback are decoupled: the decode thread converts
    // PCM and queues it, and a dedicated writer thread owns the sink.  Without
    // this a blocking pa_simple_write()/video frame stalls the other side and
    // the audio underruns (and video is paced by a jittery audio clock).
    struct AudioChunk {
        std::vector<int16_t> pcm;
        int64_t ptsMs = 0;
    };
    std::thread audioWriter;
    std::mutex audioMutex;
    std::condition_variable audioCondition;
    std::deque<AudioChunk> audioQueue;
    size_t audioQueuedFrames = 0;
    bool audioWriterStarted = false;
    bool audioWriterStop = false;
    bool audioAbortPush = false;
    bool audioFlushRequested = false;
    bool audioFlushDone = false;
    int64_t audioStartPtsMs = -1;
    int64_t audioFramesWritten = 0;
    int64_t audioClockPublishedPtsMs = -1;
    std::chrono::steady_clock::time_point audioClockPublishedWall;
    int64_t maxSyncDriftMs = 0;
    int64_t syncSamples = 0;
    bool firstFrameLogged = false;
    bool firstAudioLogged = false;
    bool volumeLogged = false;
    std::chrono::steady_clock::time_point clockWall;
    int64_t clockPtsMs = 0;

    SwsContext* scaler = nullptr;
    int scalerSrcWidth = 0;
    int scalerSrcHeight = 0;
    int scalerDstWidth = 0;
    int scalerDstHeight = 0;
    AVPixelFormat scalerSrcFormat = AV_PIX_FMT_NONE;
    std::vector<uint8_t> scaleBuffer;
};

jfieldID gContextField = nullptr;
jclass gMediaPlayerClass = nullptr;
jmethodID gPostEvent = nullptr;

void postEvent(PlayerContext* context, int what, int arg1, int arg2 = 0) {
    if (context == nullptr || context->vm == nullptr || context->weakRef == nullptr ||
        gMediaPlayerClass == nullptr || gPostEvent == nullptr) {
        return;
    }
    JNIEnv* env = nullptr;
    bool attached = false;
    if (context->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (context->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attached = true;
    }
    env->CallStaticVoidMethod(gMediaPlayerClass, gPostEvent, context->weakRef, what, arg1,
                              arg2, nullptr);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (attached) {
        context->vm->DetachCurrentThread();
    }
}

void closeCodecs(PlayerContext* context) {
    if (context->video != nullptr) {
        avcodec_free_context(&context->video);
    }
    if (context->audio != nullptr) {
        avcodec_free_context(&context->audio);
    }
    if (context->hwDevice != nullptr) {
        av_buffer_unref(&context->hwDevice);
    }
    if (context->resampler != nullptr) {
        swr_free(&context->resampler);
    }
    context->sink.close();
    context->hardwareDecode = false;
}

void closeFormat(PlayerContext* context) {
    closeCodecs(context);
    if (context->scaler != nullptr) {
        sws_freeContext(context->scaler);
        context->scaler = nullptr;
        context->scalerSrcWidth = 0;
        context->scalerSrcHeight = 0;
        context->scalerDstWidth = 0;
        context->scalerDstHeight = 0;
        context->scalerSrcFormat = AV_PIX_FMT_NONE;
    }
    if (context->format != nullptr) {
        avformat_close_input(&context->format);
    }
    if (context->avio != nullptr) {
        av_freep(&context->avio->buffer);
        avio_context_free(&context->avio);
    } else {
        av_freep(&context->avioBuffer);
    }
    if (context->source != nullptr) {
        if (context->source->fd >= 0) {
            close(context->source->fd);
        }
        delete context->source;
        context->source = nullptr;
    }
    context->videoStream = -1;
    context->audioStream = -1;
    context->prepared = false;
    context->durationMs = 0;
    context->videoWidth = 0;
    context->videoHeight = 0;
}

void stopAudioWriter(PlayerContext* context);

void stopWorker(PlayerContext* context) {
    {
        std::lock_guard lock(context->mutex);
        context->releaseRequested = true;
        context->playing = false;
    }
    {
        // Wake a decode thread that is waiting for audio queue space.
        std::lock_guard lock(context->audioMutex);
        context->audioAbortPush = true;
        context->audioCondition.notify_all();
    }
    context->condition.notify_all();
    if (context->worker.joinable()) {
        context->worker.join();
    }
    context->workerStarted = false;
    stopAudioWriter(context);
}

void releaseContext(PlayerContext* context) {
    if (context == nullptr) {
        return;
    }
    stopWorker(context);
    closeFormat(context);
    if (context->window != nullptr) {
        ANativeWindow_release(context->window);
        context->window = nullptr;
    }
    if (context->weakRef != nullptr) {
        JNIEnv* env = nullptr;
        bool attached = false;
        if (context->vm != nullptr &&
            context->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            if (context->vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            }
        }
        if (env != nullptr) {
            env->DeleteGlobalRef(context->weakRef);
        }
        if (attached) {
            context->vm->DetachCurrentThread();
        }
    }
    delete context;
}

PlayerContext* getContext(JNIEnv* env, jobject thiz) {
    return reinterpret_cast<PlayerContext*>(env->GetLongField(thiz, gContextField));
}

int64_t streamPtsMs(const AVStream* stream, int64_t pts) {
    if (pts == AV_NOPTS_VALUE) {
        return 0;
    }
    return av_rescale_q(pts, stream->time_base, AVRational{1, 1000});
}

void clearWindowBuffer(ANativeWindow_Buffer* buffer, int32_t format) {
    const int32_t width = buffer->width;
    const int32_t height = buffer->height;
    auto* base = static_cast<uint8_t*>(buffer->bits);
    if (format == 4) {  // RGB_565
        for (int32_t y = 0; y < height; y++) {
            std::memset(base + static_cast<size_t>(y) * buffer->stride * 2, 0, width * 2);
        }
        return;
    }
    for (int32_t y = 0; y < height; y++) {
        uint32_t* row = reinterpret_cast<uint32_t*>(
                base + static_cast<size_t>(y) * buffer->stride * 4);
        const uint32_t black = (format == 5) ? 0x000000FFu : 0xFF000000u;
        for (int32_t x = 0; x < width; x++) {
            row[x] = black;
        }
    }
}

void presentFrame(PlayerContext* context, AVFrame* frame) {
    if (context->window == nullptr || frame == nullptr) {
        return;
    }
    ANativeWindow_Buffer buffer;
    ARect dirty = {0, 0, 0, 0};
    if (ANativeWindow_lock(context->window, &buffer, &dirty) != 0) {
        return;
    }
    clearWindowBuffer(&buffer, buffer.format);

    const int32_t bufWidth = buffer.width;
    const int32_t bufHeight = buffer.height;
    const int srcWidth = frame->width;
    const int srcHeight = frame->height;
    if (srcWidth <= 0 || srcHeight <= 0 || bufWidth <= 0 || bufHeight <= 0) {
        ANativeWindow_unlockAndPost(context->window);
        return;
    }

    double scale = std::min(static_cast<double>(bufWidth) / srcWidth,
                            static_cast<double>(bufHeight) / srcHeight);
    int dstWidth = std::max(1, static_cast<int>(srcWidth * scale));
    int dstHeight = std::max(1, static_cast<int>(srcHeight * scale));
    const int dstX = (bufWidth - dstWidth) / 2;
    const int dstY = (bufHeight - dstHeight) / 2;

    if (context->scaler == nullptr || context->scalerSrcWidth != srcWidth ||
        context->scalerSrcHeight != srcHeight ||
        context->scalerSrcFormat != static_cast<AVPixelFormat>(frame->format) ||
        context->scalerDstWidth != dstWidth || context->scalerDstHeight != dstHeight) {
        if (context->scaler != nullptr) {
            sws_freeContext(context->scaler);
        }
        context->scaler = sws_getContext(srcWidth, srcHeight, static_cast<AVPixelFormat>(frame->format),
                                         dstWidth, dstHeight, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        context->scalerSrcWidth = srcWidth;
        context->scalerSrcHeight = srcHeight;
        context->scalerSrcFormat = static_cast<AVPixelFormat>(frame->format);
        context->scalerDstWidth = dstWidth;
        context->scalerDstHeight = dstHeight;
        context->scaleBuffer.resize(static_cast<size_t>(dstWidth) * dstHeight * 4);
    }
    if (context->scaler == nullptr) {
        ANativeWindow_unlockAndPost(context->window);
        return;
    }
    std::vector<uint8_t>& pixels = context->scaleBuffer;
    uint8_t* dstData[1] = {pixels.data()};
    int dstLinesize[1] = {dstWidth * 4};
    sws_scale(context->scaler, frame->data, frame->linesize, 0, srcHeight, dstData,
              dstLinesize);

    const int32_t format = buffer.format;
    auto* base = static_cast<uint8_t*>(buffer.bits);
    const int32_t copyWidth = std::min(dstWidth, bufWidth - dstX);
    const int32_t copyHeight = std::min(dstHeight, bufHeight - dstY);
    for (int32_t y = 0; y < copyHeight; y++) {
        const uint8_t* src = pixels.data() + static_cast<size_t>(y) * dstWidth * 4;
        if (format == 4) {
            auto* row = reinterpret_cast<uint16_t*>(
                    base + static_cast<size_t>(dstY + y) * buffer.stride * 2) + dstX;
            for (int32_t x = 0; x < copyWidth; x++) {
                row[x] = static_cast<uint16_t>(((src[x * 4 + 0] >> 3) << 11) |
                                               ((src[x * 4 + 1] >> 2) << 5) |
                                               (src[x * 4 + 2] >> 3));
            }
        } else if (format == 5) {  // BGRA
            auto* row = reinterpret_cast<uint8_t*>(
                    base + static_cast<size_t>(dstY + y) * buffer.stride * 4) + dstX * 4;
            for (int32_t x = 0; x < copyWidth; x++) {
                row[x * 4 + 0] = src[x * 4 + 2];
                row[x * 4 + 1] = src[x * 4 + 1];
                row[x * 4 + 2] = src[x * 4 + 0];
                row[x * 4 + 3] = 0xFF;
            }
        } else {  // RGBA / RGBX
            auto* row = reinterpret_cast<uint8_t*>(
                    base + static_cast<size_t>(dstY + y) * buffer.stride * 4) + dstX * 4;
            for (int32_t x = 0; x < copyWidth; x++) {
                row[x * 4 + 0] = src[x * 4 + 0];
                row[x * 4 + 1] = src[x * 4 + 1];
                row[x * 4 + 2] = src[x * 4 + 2];
                row[x * 4 + 3] = 0xFF;
            }
        }
    }
    ANativeWindow_unlockAndPost(context->window);
    if (!context->firstFrameLogged) {
        context->firstFrameLogged = true;
        ALOGI("first video frame %dx%d -> %dx%d buffer", srcWidth, srcHeight, bufWidth,
              bufHeight);
    }
}

bool openAudioDecoder(PlayerContext* context) {
    if (context->audioStream < 0) {
        return false;
    }
    AVStream* stream = context->format->streams[context->audioStream];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        return false;
    }
    context->audio = avcodec_alloc_context3(codec);
    if (context->audio == nullptr) {
        return false;
    }
    if (avcodec_parameters_to_context(context->audio, stream->codecpar) < 0 ||
        avcodec_open2(context->audio, codec, nullptr) < 0) {
        avcodec_free_context(&context->audio);
        return false;
    }
    AVChannelLayout inputLayout = context->audio->ch_layout;
    if (inputLayout.nb_channels == 0) {
        av_channel_layout_default(&inputLayout, 2);
    }
    AVChannelLayout outputLayout = AV_CHANNEL_LAYOUT_STEREO;
    int inputRate = context->audio->sample_rate > 0 ? context->audio->sample_rate : 48000;
    if (swr_alloc_set_opts2(&context->resampler, &outputLayout, AV_SAMPLE_FMT_S16, 48000,
                            &inputLayout, context->audio->sample_fmt, inputRate, 0,
                            nullptr) < 0 ||
        swr_init(context->resampler) < 0) {
        swr_free(&context->resampler);
        return false;
    }
    return true;
}

bool openAudioSink(PlayerContext* context) {
    const char* choice = std::getenv("DROIDLET_MEDIA_AUDIO");
    if (choice != nullptr && std::strcmp(choice, "none") == 0) {
        return false;
    }
    return context->sink.open("droidlet-media", 48000, 2, choice, false);
}

void playAudioFrame(PlayerContext* context, AVFrame* frame, int64_t ptsMs) {
    if (!context->sink.isOpen() || context->resampler == nullptr || frame == nullptr) {
        return;
    }
    int outSamples = swr_get_out_samples(context->resampler, frame->nb_samples);
    if (outSamples <= 0) {
        return;
    }
    std::vector<uint8_t> output(static_cast<size_t>(outSamples) * 2 * 2);
    uint8_t* outData[1] = {output.data()};
    int converted = swr_convert(context->resampler, outData, outSamples,
                                const_cast<const uint8_t**>(frame->extended_data),
                                frame->nb_samples);
    if (converted <= 0) {
        return;
    }
    // Hand the PCM to the sink writer thread.  Waiting for space also applies
    // backpressure if the sink stops consuming, without blocking video decode
    // on the sink itself.
    constexpr size_t kMaxQueuedFrames = 48000;  // one second of stereo audio
    PlayerContext::AudioChunk chunk;
    chunk.pcm.assign(reinterpret_cast<const int16_t*>(output.data()),
                     reinterpret_cast<const int16_t*>(output.data()) +
                             static_cast<size_t>(converted) * 2);
    chunk.ptsMs = ptsMs;
    {
        std::unique_lock lock(context->audioMutex);
        context->audioCondition.wait(lock, [context] {
            return context->audioAbortPush || context->audioWriterStop ||
                   context->audioQueuedFrames < kMaxQueuedFrames;
        });
        if (context->audioAbortPush || context->audioWriterStop) {
            return;
        }
        context->audioQueuedFrames += static_cast<size_t>(converted);
        context->audioQueue.push_back(std::move(chunk));
        context->audioCondition.notify_all();
    }
}

// Called by the audio writer thread after each successful sink write.  The
// master clock interpolates from the last audio position on the wall clock so
// callers never see the sink's jittery latency estimate; it only re-anchors on
// a large discontinuity (underrun, device restart, seek).
void publishAudioClockLocked(PlayerContext* context) {
    if (!context->sink.isOpen() || context->sink.isNull() ||
        context->audioStartPtsMs < 0 || context->audioFramesWritten <= 0) {
        return;
    }
    const int64_t delayedFrames = context->sink.latencyFrames();
    int64_t playedFrames = context->audioFramesWritten - delayedFrames;
    if (playedFrames < 0) {
        playedFrames = 0;
    }
    const int64_t audioMs =
            context->audioStartPtsMs + playedFrames * 1000 / context->sink.sampleRate();
    const auto now = std::chrono::steady_clock::now();
    if (context->audioClockPublishedPtsMs < 0) {
        context->audioClockPublishedPtsMs = audioMs;
        context->audioClockPublishedWall = now;
        return;
    }
    const int64_t wallMs = context->audioClockPublishedPtsMs +
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - context->audioClockPublishedWall).count();
    // Follow the audio clock when it runs ahead (it usually means the sink's
    // latency estimate shrank), but only step backwards after a large
    // discontinuity: repeated small backward corrections make the video pacer
    // sleep extra on every frame.
    if (audioMs - wallMs > 150 || wallMs - audioMs > 500) {
        context->audioClockPublishedPtsMs = audioMs;
        context->audioClockPublishedWall = now;
    }
}

int64_t audioClockMs(PlayerContext* context) {
    std::lock_guard lock(context->audioMutex);
    if (context->audioClockPublishedPtsMs < 0) {
        return -1;
    }
    return context->audioClockPublishedPtsMs +
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() -
                    context->audioClockPublishedWall).count();
}

void audioWriterLoop(PlayerContext* context) {
    for (;;) {
        PlayerContext::AudioChunk chunk;
        {
            std::unique_lock lock(context->audioMutex);
            context->audioCondition.wait(lock, [context] {
                return context->audioWriterStop || context->audioFlushRequested ||
                       !context->audioQueue.empty();
            });
            if (context->audioFlushRequested) {
                context->audioQueue.clear();
                context->audioQueuedFrames = 0;
                context->sink.flush();
                context->audioStartPtsMs = -1;
                context->audioFramesWritten = 0;
                context->audioClockPublishedPtsMs = -1;
                context->audioFlushRequested = false;
                context->audioFlushDone = true;
                context->audioCondition.notify_all();
                continue;
            }
            if (context->audioWriterStop && context->audioQueue.empty()) {
                break;
            }
            if (context->audioQueue.empty()) {
                continue;
            }
            chunk = std::move(context->audioQueue.front());
            context->audioQueue.pop_front();
            context->audioQueuedFrames -= chunk.pcm.size() / 2;
            context->audioCondition.notify_all();
        }
        const size_t frames = chunk.pcm.size() / 2;
        if (!context->sink.write(chunk.pcm.data(), static_cast<int>(frames))) {
            ALOGE("audio write failed");
        }
        {
            std::lock_guard lock(context->audioMutex);
            if (context->audioStartPtsMs < 0) {
                context->audioStartPtsMs = chunk.ptsMs;
                context->audioClockPublishedPtsMs = chunk.ptsMs;
                context->audioClockPublishedWall = std::chrono::steady_clock::now();
            }
            context->audioFramesWritten += static_cast<int64_t>(frames);
            publishAudioClockLocked(context);
        }
    }
}

void startAudioWriter(PlayerContext* context) {
    if (context->audioWriterStarted) {
        return;
    }
    {
        std::lock_guard lock(context->audioMutex);
        context->audioWriterStop = false;
        context->audioAbortPush = false;
        context->audioFlushRequested = false;
        context->audioFlushDone = false;
        context->audioQueue.clear();
        context->audioQueuedFrames = 0;
        context->audioClockPublishedPtsMs = -1;
    }
    context->audioWriterStarted = true;
    context->audioWriter = std::thread(audioWriterLoop, context);
}

void stopAudioWriter(PlayerContext* context) {
    if (!context->audioWriterStarted) {
        return;
    }
    {
        std::lock_guard lock(context->audioMutex);
        context->audioAbortPush = true;
        context->audioWriterStop = true;
        context->audioCondition.notify_all();
    }
    if (context->audioWriter.joinable()) {
        context->audioWriter.join();
    }
    context->audioWriterStarted = false;
    {
        std::lock_guard lock(context->audioMutex);
        context->audioQueue.clear();
        context->audioQueuedFrames = 0;
    }
}

// Flush the sink from the writer thread (the sink is not thread-safe).
void requestAudioFlush(PlayerContext* context) {
    std::unique_lock lock(context->audioMutex);
    if (!context->audioWriterStarted) {
        context->sink.flush();
        context->audioStartPtsMs = -1;
        context->audioFramesWritten = 0;
        context->audioClockPublishedPtsMs = -1;
        return;
    }
    context->audioFlushDone = false;
    context->audioFlushRequested = true;
    context->audioCondition.notify_all();
    context->audioCondition.wait(lock, [context] { return context->audioFlushDone; });
}

bool openVideoDecoder(PlayerContext* context) {
    if (context->videoStream < 0) {
        return false;
    }
    AVStream* stream = context->format->streams[context->videoStream];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        return false;
    }
    context->video = avcodec_alloc_context3(codec);
    if (context->video == nullptr) {
        return false;
    }
    if (avcodec_parameters_to_context(context->video, stream->codecpar) < 0) {
        avcodec_free_context(&context->video);
        return false;
    }

    // Try VAAPI on the host render node; fall back to software decode.
    const char* hwdec = std::getenv("DROIDLET_MEDIA_HWDEC");
    if (hwdec == nullptr || std::strcmp(hwdec, "none") != 0) {
        if (context->hwDevice == nullptr) {
            if (av_hwdevice_ctx_create(&context->hwDevice, AV_HWDEVICE_TYPE_VAAPI,
                                       "/dev/dri/renderD128", nullptr, 0) == 0) {
                context->video->hw_device_ctx = av_buffer_ref(context->hwDevice);
                context->video->get_format = [](AVCodecContext*,
                                                const AVPixelFormat* formats) {
                    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE;
                         format++) {
                        if (*format == AV_PIX_FMT_VAAPI) {
                            return *format;
                        }
                    }
                    return formats[0];
                };
                ALOGI("using VAAPI decode on /dev/dri/renderD128");
            } else {
                ALOGI("VAAPI unavailable; using software decode");
            }
        } else {
            context->video->hw_device_ctx = av_buffer_ref(context->hwDevice);
            context->video->get_format = [](AVCodecContext*,
                                            const AVPixelFormat* formats) {
                for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE;
                     format++) {
                    if (*format == AV_PIX_FMT_VAAPI) {
                        return *format;
                    }
                }
                return formats[0];
            };
        }
    }
    if (avcodec_open2(context->video, codec, nullptr) < 0) {
        avcodec_free_context(&context->video);
        if (context->hwDevice != nullptr) {
            av_buffer_unref(&context->hwDevice);
        }
        return false;
    }
    context->hardwareDecode = context->video->hw_device_ctx != nullptr;
    context->videoWidth = stream->codecpar->width;
    context->videoHeight = stream->codecpar->height;
    return true;
}


void seekTo(PlayerContext* context, int64_t targetMs) {
    if (context->format == nullptr) {
        return;
    }
    const int streamIndex = context->videoStream >= 0 ? context->videoStream : context->audioStream;
    if (streamIndex < 0) {
        return;
    }
    AVStream* stream = context->format->streams[streamIndex];
    int64_t target = av_rescale_q(targetMs, AVRational{1, 1000}, stream->time_base);
    av_seek_frame(context->format, streamIndex, target, AVSEEK_FLAG_BACKWARD);
    if (context->video != nullptr) {
        avcodec_flush_buffers(context->video);
    }
    if (context->audio != nullptr) {
        avcodec_flush_buffers(context->audio);
    }
    context->positionMs = targetMs;
    context->clockValid = false;
    requestAudioFlush(context);
}

void workerLoop(PlayerContext* context) {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorIo);
        return;
    }

    while (true) {
        {
            std::unique_lock lock(context->mutex);
            context->condition.wait(lock, [&] {
                return context->releaseRequested || context->seekRequested ||
                        context->playing || context->stopRequested;
            });
            if (context->releaseRequested) {
                break;
            }
            if (context->seekRequested) {
                int64_t target = context->seekTargetMs;
                context->seekRequested = false;
                lock.unlock();
                seekTo(context, target);
                postEvent(context, kMediaSeekComplete, 0, 0);
                continue;
            }
            if (context->stopRequested) {
                context->stopRequested = false;
                context->playing = false;
                context->positionMs = 0;
                context->clockValid = false;
                lock.unlock();
                seekTo(context, 0);
                postEvent(context, kMediaStopped, 0, 0);
                continue;
            }
            if (!context->playing) {
                continue;
            }
        }

        bool endOfFile = false;
        while (true) {
            {
                std::lock_guard lock(context->mutex);
                if (context->releaseRequested || !context->playing || context->seekRequested ||
                    context->stopRequested) {
                    break;
                }
            }
            int read = av_read_frame(context->format, packet);
            if (read < 0) {
                endOfFile = true;
                break;
            }
            if (packet->stream_index == context->videoStream && context->video != nullptr) {
                if (avcodec_send_packet(context->video, packet) < 0) {
                    av_packet_unref(packet);
                    continue;
                }
                av_packet_unref(packet);
                while (avcodec_receive_frame(context->video, frame) >= 0) {
                    int64_t ptsMs = streamPtsMs(context->format->streams[context->videoStream],
                                                frame->best_effort_timestamp);
                    AVFrame* displayFrame = frame;
                    AVFrame* transferred = nullptr;
                    if (frame->format == AV_PIX_FMT_VAAPI) {
                        transferred = av_frame_alloc();
                        if (transferred == nullptr ||
                            av_hwframe_transfer_data(transferred, frame, 0) < 0) {
                            av_frame_free(&transferred);
                            av_frame_unref(frame);
                            continue;
                        }
                        transferred->best_effort_timestamp = frame->best_effort_timestamp;
                        displayFrame = transferred;
                    }
                    const int64_t masterMs = audioClockMs(context);
                    {
                        std::lock_guard lock(context->mutex);
                        context->positionMs = masterMs >= 0 ? masterMs : ptsMs;
                        if (!context->clockValid) {
                            context->clockWall = std::chrono::steady_clock::now();
                            context->clockPtsMs = ptsMs;
                            context->clockValid = true;
                        }
                    }
                    if (masterMs >= 0) {
                        // Audio is the master clock.  Never sleep indefinitely
                        // for a late frame; presentation still catches up.
                        const int64_t driftMs = ptsMs - masterMs;
                        const int64_t absDrift = driftMs < 0 ? -driftMs : driftMs;
                        if (absDrift > context->maxSyncDriftMs) {
                            context->maxSyncDriftMs = absDrift;
                        }
                        context->syncSamples++;
                        const int64_t waitMs = ptsMs - masterMs;
                        if (waitMs > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
                        }
                    } else {
                        auto target = context->clockWall +
                                std::chrono::milliseconds(ptsMs - context->clockPtsMs);
                        auto now = std::chrono::steady_clock::now();
                        if (target > now) {
                            std::this_thread::sleep_until(target);
                        }
                    }
                    presentFrame(context, displayFrame);
                    av_frame_free(&transferred);
                    av_frame_unref(frame);
                }
            } else if (packet->stream_index == context->audioStream &&
                       context->audio != nullptr) {
                int64_t ptsMs = streamPtsMs(context->format->streams[context->audioStream],
                                            packet->pts);
                if (avcodec_send_packet(context->audio, packet) >= 0) {
                    av_packet_unref(packet);
                    while (avcodec_receive_frame(context->audio, frame) >= 0) {
                        int64_t framePtsMs = frame->best_effort_timestamp == AV_NOPTS_VALUE
                                ? ptsMs
                                : streamPtsMs(context->format->streams[context->audioStream],
                                              frame->best_effort_timestamp);
                        playAudioFrame(context, frame, framePtsMs);
                        av_frame_unref(frame);
                    }
                } else {
                    av_packet_unref(packet);
                }
                if (context->videoStream < 0) {
                    // Audio-only: keep the clock moving even with a null sink.
                    {
                        std::lock_guard lock(context->mutex);
                        context->positionMs = ptsMs;
                        if (!context->clockValid) {
                            context->clockWall = std::chrono::steady_clock::now();
                            context->clockPtsMs = ptsMs;
                            context->clockValid = true;
                        }
                    }
                    auto target = context->clockWall +
                            std::chrono::milliseconds(ptsMs - context->clockPtsMs);
                    auto now = std::chrono::steady_clock::now();
                    if (target > now) {
                        std::this_thread::sleep_until(target);
                    }
                }
            } else {
                av_packet_unref(packet);
            }
        }

        if (endOfFile) {
            bool complete = false;
            {
                std::lock_guard lock(context->mutex);
                if (context->releaseRequested || context->seekRequested) {
                    continue;
                }
                if (context->looping) {
                    context->seekRequested = true;
                    context->seekTargetMs = 0;
                } else {
                    context->playing = false;
                    context->completed = true;
                    context->clockValid = false;
                    context->positionMs = context->durationMs;
                    complete = true;
                    const char* debugSync = std::getenv("DROIDLET_MEDIA_DEBUG_SYNC");
                    if (debugSync != nullptr && debugSync[0] != '\0' &&
                        context->syncSamples > 0) {
                        ALOGI("A/V drift max=%" PRId64 "ms samples=%" PRId64,
                              context->maxSyncDriftMs, context->syncSamples);
                    }
                }
            }
            if (complete) {
                postEvent(context, kMediaPlaybackComplete, 0, 0);
            }
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
}

bool preparePlayer(PlayerContext* context) {
    if (context->format == nullptr) {
        return false;
    }
    context->durationMs = context->format->duration == AV_NOPTS_VALUE
            ? 0
            : context->format->duration / (AV_TIME_BASE / 1000);
    if (context->video != nullptr) {
        avcodec_free_context(&context->video);
    }
    if (context->audio != nullptr) {
        avcodec_free_context(&context->audio);
    }
    if (context->videoStream >= 0 && !openVideoDecoder(context)) {
        return false;
    }
    if (context->audioStream >= 0) {
        openAudioDecoder(context);
        openAudioSink(context);
    }
    context->prepared = true;
    return true;
}

void startWorkerIfNeeded(PlayerContext* context) {
    if (context->workerStarted) {
        return;
    }
    if (context->sink.isOpen()) {
        startAudioWriter(context);
    }
    context->workerStarted = true;
    context->worker = std::thread(workerLoop, context);
}

// ---------------------------------------------------------------------------
// JNI methods
// ---------------------------------------------------------------------------

void nativeInit(JNIEnv*, jclass) {}

void nativeSetup(JNIEnv* env, jobject thiz, jobject weakRef, jobject /*parcel*/,
                 jint sessionId) {
    PlayerContext* context = getContext(env, thiz);
    if (context != nullptr) {
        releaseContext(context);
    }
    context = new PlayerContext();
    context->vm = nullptr;
    env->GetJavaVM(&context->vm);
    context->weakRef = env->NewGlobalRef(weakRef);
    context->audioSessionId = sessionId != 0 ? sessionId : 1;
    env->SetLongField(thiz, gContextField, reinterpret_cast<jlong>(context));
}

void setDataSourceFd(JNIEnv* env, jobject thiz, jobject fd, jlong offset, jlong length) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return;
    }
    jclass fdClass = env->FindClass("java/io/FileDescriptor");
    jfieldID descriptorField = env->GetFieldID(fdClass, "descriptor", "I");
    int rawFd = env->GetIntField(fd, descriptorField);
    int heapFd = dup(rawFd);
    if (heapFd < 0) {
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorIo);
        return;
    }
    closeFormat(context);
    auto* source = new FdSource();
    source->fd = heapFd;
    source->offset = offset;
    source->length = length;
    context->source = source;
    context->avioBuffer = static_cast<uint8_t*>(av_malloc(32768));
    if (context->avioBuffer == nullptr) {
        closeFormat(context);
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorIo);
        return;
    }
    context->avio = avio_alloc_context(context->avioBuffer, 32768, 0, source, readPacket,
                                       nullptr, seekPacket);
    context->format = avformat_alloc_context();
    if (context->avio == nullptr || context->format == nullptr) {
        closeFormat(context);
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorIo);
        return;
    }
    context->format->pb = context->avio;
    context->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&context->format, nullptr, nullptr, nullptr) < 0) {
        closeFormat(context);
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorUnsupported);
        return;
    }
    avformat_find_stream_info(context->format, nullptr);
    context->videoStream = av_find_best_stream(context->format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                               nullptr, 0);
    context->audioStream = av_find_best_stream(context->format, AVMEDIA_TYPE_AUDIO, -1, -1,
                                               nullptr, 0);
}

void setDataSourcePath(JNIEnv* env, jobject thiz, jobject /*httpServiceBinder*/, jstring path,
                       jobjectArray /*keys*/, jobjectArray /*values*/) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr || path == nullptr) {
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
    int fd = open(filename.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorIo);
        return;
    }
    struct stat statBuffer {};
    fstat(fd, &statBuffer);
    closeFormat(context);
    auto* source = new FdSource();
    source->fd = fd;
    source->offset = 0;
    source->length = statBuffer.st_size;
    context->source = source;
    context->avioBuffer = static_cast<uint8_t*>(av_malloc(32768));
    if (context->avioBuffer == nullptr) {
        closeFormat(context);
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorIo);
        return;
    }
    context->avio = avio_alloc_context(context->avioBuffer, 32768, 0, source, readPacket,
                                       nullptr, seekPacket);
    context->format = avformat_alloc_context();
    if (context->avio == nullptr || context->format == nullptr) {
        closeFormat(context);
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorIo);
        return;
    }
    context->format->pb = context->avio;
    context->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&context->format, nullptr, nullptr, nullptr) < 0) {
        closeFormat(context);
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorUnsupported);
        return;
    }
    avformat_find_stream_info(context->format, nullptr);
    context->videoStream = av_find_best_stream(context->format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                               nullptr, 0);
    context->audioStream = av_find_best_stream(context->format, AVMEDIA_TYPE_AUDIO, -1, -1,
                                               nullptr, 0);
}

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

void setVideoSurface(JNIEnv* env, jobject thiz, jobject surface) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return;
    }
    ANativeWindow* window = nullptr;
    if (surface != nullptr) {
        FromSurfaceFn fromSurface = fromSurfaceFunction();
        if (fromSurface != nullptr) {
            window = fromSurface(env, surface);
        }
    }
    if (context->window != nullptr) {
        ANativeWindow_release(context->window);
    }
    context->window = window;
    ALOGI("video surface %s", window != nullptr ? "set" : "cleared");
}

jint prepare(JNIEnv* env, jobject thiz, jobject /*parcel*/) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return -1;
    }
    if (!preparePlayer(context)) {
        postEvent(context, kMediaError, kMediaErrorUnknown, kMediaErrorUnsupported);
        return -1;
    }
    startWorkerIfNeeded(context);
    postEvent(context, kMediaPrepared, 0, 0);
    postEvent(context, kMediaSetVideoSize, context->videoWidth, context->videoHeight);
    return 0;
}

jint prepareAsync(JNIEnv* env, jobject thiz, jobject /*parcel*/) {
    return prepare(env, thiz, nullptr);
}

void start(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr || !context->prepared) {
        return;
    }
    {
        std::lock_guard lock(context->mutex);
        if (context->completed ||
            (context->durationMs > 0 && context->positionMs >= context->durationMs)) {
            // Android restarts from the beginning after PlaybackCompleted.
            context->seekRequested = true;
            context->seekTargetMs = 0;
        }
        context->playing = true;
        context->completed = false;
        context->clockValid = false;
    }
    startWorkerIfNeeded(context);
    context->condition.notify_all();
    postEvent(context, kMediaStarted, 0, 0);
}

void pausePlayback(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return;
    }
    {
        std::lock_guard lock(context->mutex);
        context->playing = false;
    }
    postEvent(context, kMediaPaused, 0, 0);
}

void stop(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return;
    }
    {
        std::lock_guard lock(context->mutex);
        context->stopRequested = true;
    }
    context->condition.notify_all();
}

void seek(JNIEnv* env, jobject thiz, jlong msec, jint /*mode*/) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return;
    }
    {
        std::lock_guard lock(context->mutex);
        context->seekRequested = true;
        context->seekTargetMs = msec;
    }
    context->condition.notify_all();
}

void reset(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return;
    }
    stopWorker(context);
    {
        std::lock_guard lock(context->mutex);
        context->releaseRequested = false;
        context->playing = false;
        context->stopRequested = false;
        context->seekRequested = false;
        context->workerStarted = false;
        context->clockValid = false;
    }
    closeFormat(context);
    if (context->window != nullptr) {
        ANativeWindow_release(context->window);
        context->window = nullptr;
    }
    context->positionMs = 0;
    context->durationMs = 0;
    context->videoWidth = 0;
    context->videoHeight = 0;
    context->completed = false;
    context->firstFrameLogged = false;
}

void release(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return;
    }
    env->SetLongField(thiz, gContextField, 0);
    releaseContext(context);
}

jint getDuration(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    return context != nullptr ? static_cast<jint>(context->durationMs) : 0;
}

jint getCurrentPosition(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    return context != nullptr ? static_cast<jint>(context->positionMs) : 0;
}

jboolean isPlaying(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return JNI_FALSE;
    }
    std::lock_guard lock(context->mutex);
    return context->playing ? JNI_TRUE : JNI_FALSE;
}

jint getVideoWidth(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    return context != nullptr ? context->videoWidth : 0;
}

jint getVideoHeight(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    return context != nullptr ? context->videoHeight : 0;
}

void setLooping(JNIEnv* env, jobject thiz, jboolean looping) {
    PlayerContext* context = getContext(env, thiz);
    if (context != nullptr) {
        std::lock_guard lock(context->mutex);
        context->looping = looping == JNI_TRUE;
    }
}

jboolean isLooping(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    if (context == nullptr) {
        return JNI_FALSE;
    }
    std::lock_guard lock(context->mutex);
    return context->looping ? JNI_TRUE : JNI_FALSE;
}

void setVolume(JNIEnv* env, jobject thiz, jfloat left, jfloat right) {
    PlayerContext* context = getContext(env, thiz);
    if (context != nullptr) {
        context->leftVolume = left;
        context->rightVolume = right;
        if (!context->volumeLogged) {
            context->volumeLogged = true;
            ALOGI("volume %.2f/%.2f", left, right);
        }
    }
}

jint getAudioSessionId(JNIEnv* env, jobject thiz) {
    PlayerContext* context = getContext(env, thiz);
    return context != nullptr ? context->audioSessionId : 1;
}

void setAudioSessionId(JNIEnv* env, jobject thiz, jint sessionId) {
    PlayerContext* context = getContext(env, thiz);
    if (context != nullptr) {
        context->audioSessionId = sessionId;
    }
}

jint getAudioStreamType(JNIEnv*, jobject) {
    return 3;  // AudioManager.STREAM_MUSIC
}

void noOpVoid(JNIEnv*, jobject) {}
void noOpVoidInt(JNIEnv*, jobject, jint) {}
void noOpVoidBool(JNIEnv*, jobject, jboolean) {}
void noOpVoidFloat(JNIEnv*, jobject, jfloat) {}
void noOpVoidLong(JNIEnv*, jobject, jlong) {}
void noOpVoidObject(JNIEnv*, jobject, jobject) {}

jboolean falseBooleanInt(JNIEnv*, jobject, jint) { return JNI_FALSE; }
jboolean falseBooleanIntObject(JNIEnv*, jobject, jint, jobject) { return JNI_FALSE; }
jboolean falseBooleanParcel(JNIEnv*, jobject, jboolean, jboolean, jobject) { return JNI_FALSE; }
jint zeroIntParcelObject(JNIEnv*, jobject, jobject, jobject) { return 0; }
jint zeroIntParcelObjectStatic(JNIEnv*, jclass, jobject) { return 0; }
jobject nullObject(JNIEnv*, jobject) { return nullptr; }
jobject nullObjectInt(JNIEnv*, jobject, jint) { return nullptr; }
jintArray nullIntArray(JNIEnv*, jobject) { return nullptr; }
jint invoke(JNIEnv* env, jobject thiz, jobject request, jobject reply) {
    (void)thiz;
    (void)request;
    if (reply != nullptr) {
        jclass parcelClass = env->FindClass("android/os/Parcel");
        jmethodID writeInt = env->GetMethodID(parcelClass, "writeInt", "(I)V");
        if (writeInt != nullptr) {
            env->CallVoidMethod(reply, writeInt, 0);
        }
    }
    return 0;
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_android_media_MediaPlayer_native_1init(JNIEnv* env, jclass clazz) {
    if (gContextField == nullptr) {
        gContextField = env->GetFieldID(clazz, "mNativeContext", "J");
    }
    if (gMediaPlayerClass == nullptr) {
        gMediaPlayerClass = reinterpret_cast<jclass>(env->NewGlobalRef(clazz));
    }
    if (gPostEvent == nullptr) {
        gPostEvent = env->GetStaticMethodID(
                clazz, "postEventFromNative",
                "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    }

    JNINativeMethod methods[] = {
            {"native_setup", "(Ljava/lang/Object;Landroid/os/Parcel;I)V",
             reinterpret_cast<void*>(nativeSetup)},
            {"native_init", "()V", reinterpret_cast<void*>(nativeInit)},
            {"_setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
             reinterpret_cast<void*>(setDataSourceFd)},
            {"_setDataSource", "(Landroid/media/MediaDataSource;)V",
             reinterpret_cast<void*>(noOpVoidObject)},
            {"nativeSetDataSource",
             "(Landroid/os/IBinder;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
             reinterpret_cast<void*>(setDataSourcePath)},
            {"_setVideoSurface", "(Landroid/view/Surface;)V",
             reinterpret_cast<void*>(setVideoSurface)},
            {"_prepare", "(Landroid/os/Parcel;)I", reinterpret_cast<void*>(prepare)},
            {"_prepareAsync", "(Landroid/os/Parcel;)I", reinterpret_cast<void*>(prepareAsync)},
            {"_start", "()V", reinterpret_cast<void*>(start)},
            {"_pause", "()V", reinterpret_cast<void*>(pausePlayback)},
            {"_stop", "()V", reinterpret_cast<void*>(stop)},
            {"_reset", "()V", reinterpret_cast<void*>(reset)},
            {"_release", "()V", reinterpret_cast<void*>(release)},
            {"native_finalize", "()V", reinterpret_cast<void*>(release)},
            {"_seekTo", "(JI)V", reinterpret_cast<void*>(seek)},
            {"getDuration", "()I", reinterpret_cast<void*>(getDuration)},
            {"getCurrentPosition", "()I", reinterpret_cast<void*>(getCurrentPosition)},
            {"isPlaying", "()Z", reinterpret_cast<void*>(isPlaying)},
            {"getVideoWidth", "()I", reinterpret_cast<void*>(getVideoWidth)},
            {"getVideoHeight", "()I", reinterpret_cast<void*>(getVideoHeight)},
            {"setLooping", "(Z)V", reinterpret_cast<void*>(setLooping)},
            {"isLooping", "()Z", reinterpret_cast<void*>(isLooping)},
            {"_setVolume", "(FF)V", reinterpret_cast<void*>(setVolume)},
            {"getAudioSessionId", "()I", reinterpret_cast<void*>(getAudioSessionId)},
            {"native_setAudioSessionId", "(I)V", reinterpret_cast<void*>(setAudioSessionId)},
            {"_getAudioStreamType", "()I", reinterpret_cast<void*>(getAudioStreamType)},
            {"_setAudioStreamType", "(I)V", reinterpret_cast<void*>(noOpVoidInt)},
            {"_notifyAt", "(J)V", reinterpret_cast<void*>(noOpVoidLong)},
            {"native_invoke", "(Landroid/os/Parcel;Landroid/os/Parcel;)I",
             reinterpret_cast<void*>(invoke)},
            {"native_getMetadata", "(ZZLandroid/os/Parcel;)Z",
             reinterpret_cast<void*>(falseBooleanParcel)},
            {"native_setMetadataFilter", "(Landroid/os/Parcel;)I",
             reinterpret_cast<void*>(zeroIntParcelObject)},
            {"setPlaybackParams", "(Landroid/media/PlaybackParams;)V",
             reinterpret_cast<void*>(noOpVoidObject)},
            {"setSyncParams", "(Landroid/media/SyncParams;)V",
             reinterpret_cast<void*>(noOpVoidObject)},
            {"setNextMediaPlayer", "(Landroid/media/MediaPlayer;)V",
             reinterpret_cast<void*>(noOpVoidObject)},
            {"attachAuxEffect", "(I)V", reinterpret_cast<void*>(noOpVoidInt)},
            {"_setAuxEffectSendLevel", "(F)V", reinterpret_cast<void*>(noOpVoidFloat)},
            {"native_applyVolumeShaper",
             "(Landroid/media/VolumeShaper$Configuration;Landroid/media/VolumeShaper$Operation;)I",
             reinterpret_cast<void*>(zeroIntParcelObject)},
            {"native_getVolumeShaperState", "(I)Landroid/media/VolumeShaper$State;",
             reinterpret_cast<void*>(nullObjectInt)},
            {"native_setOutputDevice", "(I)Z", reinterpret_cast<void*>(falseBooleanInt)},
            {"native_getRoutedDeviceIds", "()[I", reinterpret_cast<void*>(nullIntArray)},
            {"native_enableDeviceCallback", "(Z)V", reinterpret_cast<void*>(noOpVoidBool)},
            {"native_getMetrics", "()Landroid/os/PersistableBundle;",
             reinterpret_cast<void*>(nullObject)},
            {"setParameter", "(ILandroid/os/Parcel;)Z",
             reinterpret_cast<void*>(falseBooleanIntObject)},
            {"_releaseDrm", "()V", reinterpret_cast<void*>(noOpVoid)},
            {"native_pullBatteryData", "(Landroid/os/Parcel;)I",
             reinterpret_cast<void*>(zeroIntParcelObjectStatic)},
    };
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0) {
        ALOGE("MediaPlayer native registration failed");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }
}
