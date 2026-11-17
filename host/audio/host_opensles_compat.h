#pragma once

#include <pthread.h>
#include <sys/types.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#ifndef SL_ANDROID_SPEAKER_COUNT_MAX
#define SL_ANDROID_SPEAKER_COUNT_MAX 8
#endif
#ifndef SL_ANDROID_UNKNOWN_CHANNELMASK
#define SL_ANDROID_UNKNOWN_CHANNELMASK 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

pid_t pthread_gettid_np(pthread_t thread);

SLuint32 sles_channel_out_mask_from_count(unsigned channelCount);
SLuint32 sles_channel_in_mask_from_count(unsigned channelCount);
SLuint32 sles_channel_count_from_mask(SLuint32 mask);
SLboolean sles_is_channel_mask_valid(SLuint32 mask);

#ifdef __cplusplus
}
#endif
