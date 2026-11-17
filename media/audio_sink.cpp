#include "audio_sink.h"

#include <log/log.h>
#include <pulse/error.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

HostAudioSink::~HostAudioSink() {
    close();
}

bool HostAudioSink::open(const char* logTag, int sampleRate, int channels,
                         const char* preference, bool allowNull) {
    close();
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000;
    channels_ = std::max(1, channels);
    const char* tag = logTag != nullptr ? logTag : "audio";

    if (preference != nullptr && std::strcmp(preference, "none") == 0) {
        return false;
    }
    const bool explicitDevice = preference != nullptr && preference[0] != '\0' &&
            std::strcmp(preference, "pulse") != 0 &&
            std::strcmp(preference, "pipewire") != 0;
    const bool preferPulse = !explicitDevice;

    if (preferPulse) {
        pa_sample_spec spec{};
        spec.format = PA_SAMPLE_S16LE;
        spec.rate = static_cast<uint32_t>(sampleRate_);
        spec.channels = static_cast<uint8_t>(channels_);
        // MediaPlayer writes audio from the same thread that decodes and
        // presents video.  A video frame can take tens of milliseconds
        // (decode + scale + SurfaceView queue), and the server's default
        // buffer is short enough that those stalls underrun the stream;
        // recording the sink shows 10-20 ms silent holes.  Ask for ~250 ms of
        // buffering so a video hiccup does not become an audible gap.
        pa_buffer_attr attr{};
        attr.maxlength = static_cast<uint32_t>(-1);
        attr.tlength = static_cast<uint32_t>(
                static_cast<uint64_t>(sampleRate_) * channels_ * 2 / 4);  // 250 ms
        attr.prebuf = static_cast<uint32_t>(-1);
        attr.minreq = static_cast<uint32_t>(-1);
        attr.fragsize = static_cast<uint32_t>(-1);
        int error = 0;
        pulse_ = pa_simple_new(nullptr, tag, PA_STREAM_PLAYBACK, nullptr, "media", &spec,
                               nullptr, &attr, &error);
        if (pulse_ != nullptr) {
            open_ = true;
            __android_log_print(ANDROID_LOG_INFO, tag, "audio sink pulse %d Hz %d ch",
                            sampleRate_, channels_);
            return true;
        }
        __android_log_print(ANDROID_LOG_WARN, tag, "pulse sink open failed: %s",
                            pa_strerror(error));
    }

    std::vector<const char*> devices;
    if (explicitDevice) {
        devices.push_back(preference);
    }
    for (const char* device : {"default", "sysdefault", "plughw:0,0", "pulse", "pipewire"}) {
        if (std::find(devices.begin(), devices.end(), device) == devices.end()) {
            devices.push_back(device);
        }
    }
    for (const char* device : devices) {
        if (openAlsa(tag, device)) {
            return true;
        }
    }
    if (allowNull) {
        nullSink_ = true;
        open_ = true;
        nullClock_ = std::chrono::steady_clock::now();
        __android_log_print(ANDROID_LOG_INFO, tag, "audio sink null");
        return true;
    }
    __android_log_print(ANDROID_LOG_WARN, tag, "no audio sink");
    return false;
}

bool HostAudioSink::openAlsa(const char* logTag, const char* device) {
    int result = snd_pcm_open(&pcm_, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (result < 0) {
        __android_log_print(ANDROID_LOG_WARN, logTag, "audio sink %s open failed: %s",
                            device, snd_strerror(result));
        pcm_ = nullptr;
        return false;
    }
    result = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                channels_, sampleRate_, 1, 100000);
    if (result < 0) {
        __android_log_print(ANDROID_LOG_WARN, logTag, "audio sink %s params failed: %s",
                            device, snd_strerror(result));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }
    snd_pcm_nonblock(pcm_, 0);
    if (snd_pcm_prepare(pcm_) < 0) {
        __android_log_print(ANDROID_LOG_WARN, logTag, "audio sink %s prepare failed",
                            device);
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }
    snd_pcm_info_t* info = nullptr;
    snd_pcm_info_alloca(&info);
    if (snd_pcm_info(pcm_, info) == 0) {
        __android_log_print(ANDROID_LOG_INFO, logTag, "audio sink %s (%s/%s) %d Hz %d ch",
                            device, snd_pcm_info_get_name(info), snd_pcm_info_get_id(info),
                            sampleRate_, channels_);
    } else {
        __android_log_print(ANDROID_LOG_INFO, logTag, "audio sink %s %d Hz %d ch", device,
                            sampleRate_, channels_);
    }
    open_ = true;
    return true;
}

void HostAudioSink::close() {
    if (pulse_ != nullptr) {
        pa_simple_drain(pulse_, nullptr);
        pa_simple_free(pulse_);
        pulse_ = nullptr;
    }
    if (pcm_ != nullptr) {
        snd_pcm_drain(pcm_);
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
    open_ = false;
    nullSink_ = false;
    framesWritten_ = 0;
}

void HostAudioSink::setVolume(float left, float right) {
    leftVolume_ = std::clamp(left, 0.0f, 1.0f);
    rightVolume_ = std::clamp(right, 0.0f, 1.0f);
}

bool HostAudioSink::write(const int16_t* samples, int frames) {
    if (!open_ || samples == nullptr || frames <= 0) {
        return open_;
    }
    const int channels = std::max(1, channels_);
    const int16_t* output = samples;
    if (leftVolume_ != 1.0f || rightVolume_ != 1.0f) {
        const size_t count = static_cast<size_t>(frames) * channels;
        scaled_.resize(count);
        for (size_t i = 0; i < count; i++) {
            const float volume = (channels == 1 || (i % channels) == 0)
                    ? leftVolume_ : rightVolume_;
            scaled_[i] = static_cast<int16_t>(samples[i] * volume);
        }
        output = scaled_.data();
    }
    if (nullSink_) {
        framesWritten_ += frames;
        return true;
    }
    if (pulse_ != nullptr) {
        int error = 0;
        if (pa_simple_write(pulse_, output,
                            static_cast<size_t>(frames) * channels * sizeof(int16_t),
                            &error) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, "droidlet-audio-sink",
                                "pulse write failed: %s", pa_strerror(error));
            return false;
        }
        framesWritten_ += frames;
        return true;
    }
    if (pcm_ != nullptr) {
        int offset = 0;
        int remaining = frames;
        while (remaining > 0) {
            snd_pcm_sframes_t written = snd_pcm_writei(
                    pcm_, output + static_cast<size_t>(offset) * channels, remaining);
            if (written < 0) {
                written = snd_pcm_recover(pcm_, static_cast<int>(written), 1);
                if (written <= 0) {
                    __android_log_print(ANDROID_LOG_ERROR, "droidlet-audio-sink",
                                        "write failed: %s",
                                        snd_strerror(static_cast<int>(written)));
                    return false;
                }
            }
            offset += static_cast<int>(written);
            remaining -= static_cast<int>(written);
        }
        framesWritten_ += frames;
        return true;
    }
    return false;
}

int64_t HostAudioSink::latencyFrames() const {
    if (nullSink_) {
        return 0;
    }
    if (pulse_ != nullptr) {
        int error = 0;
        pa_usec_t latency = pa_simple_get_latency(pulse_, &error);
        if (latency == static_cast<pa_usec_t>(-1)) {
            return 0;
        }
        return static_cast<int64_t>(latency) * sampleRate_ / 1000000;
    }
    if (pcm_ != nullptr) {
        snd_pcm_sframes_t delay = 0;
        if (snd_pcm_delay(pcm_, &delay) == 0 && delay > 0) {
            return delay;
        }
    }
    return 0;
}

void HostAudioSink::flush() {
    framesWritten_ = 0;
    if (pulse_ != nullptr) {
        pa_simple_flush(pulse_, nullptr);
    } else if (pcm_ != nullptr) {
        snd_pcm_drop(pcm_);
        snd_pcm_prepare(pcm_);
    }
    nullClock_ = std::chrono::steady_clock::now();
}
