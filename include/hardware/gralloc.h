/*
 * Host shim for the legacy gralloc usage constants.
 *
 * droidlet has no gralloc HAL and does not build hardware/libhardware.  The
 * UI and graphics code still uses the legacy GRALLOC_USAGE_* bit values, so
 * keep just those constants here and leave the HAL module structures out.
 */

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <cutils/native_handle.h>
#include <system/graphics.h>

enum {
    GRALLOC_USAGE_SW_READ_NEVER         = 0x00000000U,
    GRALLOC_USAGE_SW_READ_RARELY        = 0x00000002U,
    GRALLOC_USAGE_SW_READ_OFTEN         = 0x00000003U,
    GRALLOC_USAGE_SW_READ_MASK          = 0x0000000FU,

    GRALLOC_USAGE_SW_WRITE_NEVER        = 0x00000000U,
    GRALLOC_USAGE_SW_WRITE_RARELY       = 0x00000020U,
    GRALLOC_USAGE_SW_WRITE_OFTEN        = 0x00000030U,
    GRALLOC_USAGE_SW_WRITE_MASK         = 0x000000F0U,

    GRALLOC_USAGE_HW_TEXTURE             = 0x00000100U,
    GRALLOC_USAGE_HW_RENDER              = 0x00000200U,
    GRALLOC_USAGE_HW_2D                  = 0x00000400U,
    GRALLOC_USAGE_HW_COMPOSER            = 0x00000800U,
    GRALLOC_USAGE_HW_FB                  = 0x00001000U,

    GRALLOC_USAGE_EXTERNAL_DISP          = 0x00002000U,
    GRALLOC_USAGE_PROTECTED              = 0x00004000U,
    GRALLOC_USAGE_CURSOR                 = 0x00008000U,

    GRALLOC_USAGE_HW_VIDEO_ENCODER       = 0x00010000U,
    GRALLOC_USAGE_HW_CAMERA_WRITE        = 0x00020000U,
    GRALLOC_USAGE_HW_CAMERA_READ         = 0x00040000U,
    GRALLOC_USAGE_HW_CAMERA_ZSL          = 0x00060000U,
    GRALLOC_USAGE_HW_CAMERA_MASK         = 0x00060000U,
    GRALLOC_USAGE_HW_MASK                = 0x00071F00U,

    GRALLOC_USAGE_RENDERSCRIPT           = 0x00100000U,
    GRALLOC_USAGE_FOREIGN_BUFFERS        = 0x00200000U,
    GRALLOC_USAGE_HW_IMAGE_ENCODER       = 0x08000000U,

    GRALLOC_USAGE_ALLOC_MASK             = ~(GRALLOC_USAGE_FOREIGN_BUFFERS),

    GRALLOC_USAGE_PRIVATE_0              = 0x10000000U,
    GRALLOC_USAGE_PRIVATE_1              = 0x20000000U,
    GRALLOC_USAGE_PRIVATE_2              = 0x40000000U,
    GRALLOC_USAGE_PRIVATE_3              = 0x80000000U,
    GRALLOC_USAGE_PRIVATE_MASK           = 0xF0000000U,
};
