/*
 * Shared host audio sink for MediaPlayer and AudioTrack.
 *
 * Output order is PulseAudio/PipeWire, then ALSA, then an optional paced null
 * sink.  PCM is interleaved 16-bit.  The sink exposes the number of frames
 * accepted and the current sink latency so callers can derive an audio clock.
 */

#ifndef DROIDLET_MEDIA_AUDIO_SINK_H_
#define DROIDLET_MEDIA_AUDIO_SINK_H_

#include <alsa/asoundlib.h>
#include <pulse/simple.h>

#include <chrono>
#include <cstdint>
#include <vector>

class HostAudioSink {
  public:
    HostAudioSink() = default;
    ~HostAudioSink();

    HostAudioSink(const HostAudioSink&) = delete;
    HostAudioSink& operator=(const HostAudioSink&) = delete;

    bool open(const char* logTag, int sampleRate, int channels, const char* preference,
              bool allowNull);
    void close();

    bool isOpen() const { return open_; }
    bool isNull() const { return nullSink_; }
    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    int64_t framesWritten() const { return framesWritten_; }

    void setVolume(float left, float right);
    bool write(const int16_t* samples, int frames);
    int64_t latencyFrames() const;
    void flush();

  private:
    bool openAlsa(const char* logTag, const char* device);

    pa_simple* pulse_ = nullptr;
    snd_pcm_t* pcm_ = nullptr;
    int sampleRate_ = 48000;
    int channels_ = 2;
    int64_t framesWritten_ = 0;
    float leftVolume_ = 1.0f;
    float rightVolume_ = 1.0f;
    bool open_ = false;
    bool nullSink_ = false;
    std::chrono::steady_clock::time_point nullClock_;
    std::vector<int16_t> scaled_;
};

#endif  // DROIDLET_MEDIA_AUDIO_SINK_H_
