/*
 * Host codec registry shared by MediaCodecList and MediaCodec.
 *
 * The host has a single ffmpeg-backed decoder per MIME type.  We advertise
 * them with the c2.android.* naming scheme so apps and libraries that use
 * MediaCodecList (ExoPlayer, MediaPlayer2, ...) can discover and select them.
 */

#ifndef DROIDLET_MEDIA_CODEC_REGISTRY_H_
#define DROIDLET_MEDIA_CODEC_REGISTRY_H_

#include <cstddef>

struct HostCodecInfo {
    const char* name;
    const char* mime;
    // CodecProfileLevel constants, or 0 when the codec does not expose them.
    int profile;
    int level;
    // MediaCodecInfo FLAG_IS_SOFTWARE_ONLY (1 << 2).
    int attributes;
};

const HostCodecInfo* hostCodecs(size_t* count);
const HostCodecInfo* hostCodecForName(const char* name);
const HostCodecInfo* hostCodecForMime(const char* mime);
const char* hostMimeForCodecName(const char* name);

#endif  // DROIDLET_MEDIA_CODEC_REGISTRY_H_
