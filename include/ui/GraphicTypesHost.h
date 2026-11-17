/*
 * Host replacements for the HIDL graphics enums used by ui/GraphicTypes.h.
 *
 * The host build does not need the HIDL runtime; only the stable enum values
 * are part of the public android::ui API.  Values match HIDL 1.0/1.1/1.2.
 */

#pragma once

#include <cstdint>

namespace android {
namespace ui {

enum class RenderIntent : int32_t {
    COLORIMETRIC = 0,
    ENHANCE = 1,
    TONE_MAP_COLORIMETRIC = 2,
    TONE_MAP_ENHANCE = 3,
};

enum class Dataspace : int32_t {
    UNKNOWN = 0,
    ARBITRARY = 1,
    STANDARD_SHIFT = 16,
    STANDARD_MASK = 4128768,
    STANDARD_UNSPECIFIED = 0,
    STANDARD_BT709 = 65536,
    STANDARD_BT601_625 = 131072,
    STANDARD_BT601_625_UNADJUSTED = 196608,
    STANDARD_BT601_525 = 262144,
    STANDARD_BT601_525_UNADJUSTED = 327680,
    STANDARD_BT2020 = 393216,
    STANDARD_BT2020_CONSTANT_LUMINANCE = 458752,
    STANDARD_BT470M = 524288,
    STANDARD_FILM = 589824,
    STANDARD_DCI_P3 = 655360,
    STANDARD_ADOBE_RGB = 720896,
    TRANSFER_SHIFT = 22,
    TRANSFER_MASK = 130023424,
    TRANSFER_UNSPECIFIED = 0,
    TRANSFER_LINEAR = 4194304,
    TRANSFER_SRGB = 8388608,
    TRANSFER_SMPTE_170M = 12582912,
    TRANSFER_GAMMA2_2 = 16777216,
    TRANSFER_GAMMA2_6 = 20971520,
    TRANSFER_GAMMA2_8 = 25165824,
    TRANSFER_ST2084 = 29360128,
    TRANSFER_HLG = 33554432,
    RANGE_SHIFT = 27,
    RANGE_MASK = 939524096,
    RANGE_UNSPECIFIED = 0,
    RANGE_FULL = 134217728,
    RANGE_LIMITED = 268435456,
    RANGE_EXTENDED = 402653184,
    SRGB_LINEAR = 512,
    V0_SRGB_LINEAR = 138477568,
    V0_SCRGB_LINEAR = 406913024,
    SRGB = 513,
    V0_SRGB = 142671872,
    V0_SCRGB = 411107328,
    JFIF = 257,
    V0_JFIF = 146931712,
    BT601_625 = 258,
    V0_BT601_625 = 281149440,
    BT601_525 = 259,
    V0_BT601_525 = 281280512,
    BT709 = 260,
    V0_BT709 = 281083904,
    DCI_P3_LINEAR = 139067392,
    DCI_P3 = 155844608,
    DISPLAY_P3_LINEAR = 139067392,
    DISPLAY_P3 = 143261696,
    ADOBE_RGB = 151715840,
    BT2020_LINEAR = 138805248,
    BT2020 = 147193856,
    BT2020_PQ = 163971072,
    DEPTH = 4096,
    SENSOR = 4097,
    BT2020_ITU = 281411584,
    BT2020_ITU_PQ = 298188800,
    BT2020_ITU_HLG = 302383104,
    BT2020_HLG = 168165376,
    DISPLAY_BT2020 = 142999552,
    DYNAMIC_DEPTH = 4098,
    JPEG_APP_SEGMENTS = 4099,
    HEIF = 4100,
};

enum class ColorMode : int32_t {
    NATIVE = 0,
    STANDARD_BT601_625 = 1,
    STANDARD_BT601_625_UNADJUSTED = 2,
    STANDARD_BT601_525 = 3,
    STANDARD_BT601_525_UNADJUSTED = 4,
    STANDARD_BT709 = 5,
    DCI_P3 = 6,
    SRGB = 7,
    ADOBE_RGB = 8,
    DISPLAY_P3 = 9,
    BT2020 = 10,
    BT2100_PQ = 11,
    BT2100_HLG = 12,
    DISPLAY_BT2020 = 13,
};

enum class PixelFormat : int32_t {
    RGBA_8888 = 1,
    RGBX_8888 = 2,
    RGB_888 = 3,
    RGB_565 = 4,
    BGRA_8888 = 5,
    YCBCR_422_SP = 16,
    YCRCB_420_SP = 17,
    YCBCR_422_I = 20,
    RGBA_FP16 = 22,
    RAW16 = 32,
    BLOB = 33,
    IMPLEMENTATION_DEFINED = 34,
    YCBCR_420_888 = 35,
    RAW_OPAQUE = 36,
    RAW10 = 37,
    RAW12 = 38,
    RGBA_1010102 = 43,
    Y8 = 538982489,
    Y16 = 540422489,
    YV12 = 842094169,
    DEPTH_16 = 48,
    DEPTH_24 = 49,
    DEPTH_24_STENCIL_8 = 50,
    DEPTH_32F = 51,
    DEPTH_32F_STENCIL_8 = 52,
    STENCIL_8 = 53,
    YCBCR_P010 = 54,
    HSV_888 = 55,
};


// HIDL generates bitwise helpers for these enums; keep the same operations
// available for the host enums used by ui/GraphicTypes.h.
#define DROIDLET_UI_ENUM_OPS(Enum)                                             \
    constexpr int32_t operator|(Enum lhs, Enum rhs) {                          \
        return static_cast<int32_t>(lhs) | static_cast<int32_t>(rhs);          \
    }                                                                          \
    constexpr int32_t operator|(int32_t lhs, Enum rhs) {                       \
        return lhs | static_cast<int32_t>(rhs);                                \
    }                                                                          \
    constexpr int32_t operator|(Enum lhs, int32_t rhs) {                       \
        return static_cast<int32_t>(lhs) | rhs;                                \
    }                                                                          \
    constexpr int32_t operator&(Enum lhs, Enum rhs) {                          \
        return static_cast<int32_t>(lhs) & static_cast<int32_t>(rhs);          \
    }                                                                          \
    constexpr int32_t operator&(int32_t lhs, Enum rhs) {                       \
        return lhs & static_cast<int32_t>(rhs);                                \
    }                                                                          \
    constexpr int32_t operator&(Enum lhs, int32_t rhs) {                       \
        return static_cast<int32_t>(lhs) & rhs;                                \
    }                                                                          \
    constexpr int32_t& operator|=(int32_t& lhs, Enum rhs) {                    \
        lhs |= static_cast<int32_t>(rhs);                                      \
        return lhs;                                                            \
    }                                                                          \
    constexpr int32_t& operator&=(int32_t& lhs, Enum rhs) {                    \
        lhs &= static_cast<int32_t>(rhs);                                      \
        return lhs;                                                            \
    }

DROIDLET_UI_ENUM_OPS(Dataspace)
DROIDLET_UI_ENUM_OPS(ColorMode)
DROIDLET_UI_ENUM_OPS(PixelFormat)

#undef DROIDLET_UI_ENUM_OPS
}  // namespace ui
}  // namespace android
