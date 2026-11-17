#include "codec_registry.h"

#include <cstring>

namespace {

// MediaCodecInfo.FLAG_IS_SOFTWARE_ONLY
constexpr int kSoftwareOnly = 1 << 2;

// MediaCodecInfo.CodecProfileLevel constants.
constexpr int kAvcProfileHigh = 0x08;
constexpr int kAvcLevel51 = 0x8000;
constexpr int kHevcProfileMain = 0x01;
constexpr int kHevcMainTierLevel51 = 0x10000;
constexpr int kMpeg4ProfileSimple = 0x01;
constexpr int kMpeg4Level5 = 0x80;
constexpr int kH263ProfileBaseline = 0x01;
constexpr int kH263Level45 = 0x10;
constexpr int kVP8ProfileMain = 0x01;
constexpr int kVP8LevelVersion0 = 0x01;
constexpr int kVP9Profile0 = 0x01;
constexpr int kVP9Level51 = 0x200;
constexpr int kAV1ProfileMain8 = 0x01;
constexpr int kAV1Level51 = 0x2000;
constexpr int kAACObjectLC = 2;

const HostCodecInfo kCodecs[] = {
        {"c2.android.avc.decoder", "video/avc", kAvcProfileHigh, kAvcLevel51,
         kSoftwareOnly},
        {"c2.android.hevc.decoder", "video/hevc", kHevcProfileMain, kHevcMainTierLevel51,
         kSoftwareOnly},
        {"c2.android.mpeg4.decoder", "video/mp4v-es", kMpeg4ProfileSimple, kMpeg4Level5,
         kSoftwareOnly},
        {"c2.android.h263.decoder", "video/3gpp", kH263ProfileBaseline, kH263Level45,
         kSoftwareOnly},
        {"c2.android.vp8.decoder", "video/x-vnd.on2.vp8", kVP8ProfileMain,
         kVP8LevelVersion0, kSoftwareOnly},
        {"c2.android.vp9.decoder", "video/x-vnd.on2.vp9", kVP9Profile0, kVP9Level51,
         kSoftwareOnly},
        {"c2.android.av1.decoder", "video/av01", kAV1ProfileMain8, kAV1Level51,
         kSoftwareOnly},
        {"c2.android.aac.decoder", "audio/mp4a-latm", kAACObjectLC, 0, kSoftwareOnly},
        {"c2.android.mp3.decoder", "audio/mpeg", 0, 0, kSoftwareOnly},
        {"c2.android.opus.decoder", "audio/opus", 0, 0, kSoftwareOnly},
        {"c2.android.vorbis.decoder", "audio/vorbis", 0, 0, kSoftwareOnly},
        {"c2.android.flac.decoder", "audio/flac", 0, 0, kSoftwareOnly},
        {"c2.android.raw.decoder", "audio/raw", 0, 0, kSoftwareOnly},
};

constexpr size_t kCodecCount = sizeof(kCodecs) / sizeof(kCodecs[0]);

}  // namespace

const HostCodecInfo* hostCodecs(size_t* count) {
    if (count != nullptr) {
        *count = kCodecCount;
    }
    return kCodecs;
}

const HostCodecInfo* hostCodecForName(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (const HostCodecInfo& codec : kCodecs) {
        if (std::strcmp(codec.name, name) == 0) {
            return &codec;
        }
    }
    return nullptr;
}

const char* hostMimeForCodecName(const char* name) {
    const HostCodecInfo* codec = hostCodecForName(name);
    return codec != nullptr ? codec->mime : nullptr;
}

const HostCodecInfo* hostCodecForMime(const char* mime) {
    if (mime == nullptr) {
        return nullptr;
    }
    for (const HostCodecInfo& codec : kCodecs) {
        if (std::strcmp(codec.mime, mime) == 0) {
            return &codec;
        }
    }
    return nullptr;
}
