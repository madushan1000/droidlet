#include "host_opensles_compat.h"

#include <cstdint>

pid_t pthread_gettid_np(pthread_t) {
    return -1;
}

SLuint32 sles_channel_out_mask_from_count(unsigned channelCount) {
    switch (channelCount) {
        case 1:
            return SL_SPEAKER_FRONT_LEFT;
        case 2:
            return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
        default:
            return SL_ANDROID_UNKNOWN_CHANNELMASK;
    }
}

SLuint32 sles_channel_in_mask_from_count(unsigned channelCount) {
    return sles_channel_out_mask_from_count(channelCount);
}

SLuint32 sles_channel_count_from_mask(SLuint32 mask) {
    return static_cast<SLuint32>(__builtin_popcount(mask));
}

SLboolean sles_is_channel_mask_valid(SLuint32) {
    return SL_BOOLEAN_TRUE;
}
