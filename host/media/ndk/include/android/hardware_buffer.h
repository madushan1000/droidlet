/* Minimal host shim: the media headers only use AHardwareBuffer opaquely. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct AHardwareBuffer;
typedef struct AHardwareBuffer AHardwareBuffer;

enum {
    AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM = 1,
    AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM = 2,
    AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM = 4,
};

#ifdef __cplusplus
}
#endif
