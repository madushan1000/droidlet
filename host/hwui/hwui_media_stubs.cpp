/*
 * Host link stubs for libhwui.
 *
 * droidlet builds libhwui for the host, where libEGL and libmediandk are not
 * present.  Enabling android_graphics_HardwareRenderer.cpp requires these
 * symbols at link time.  Activity launch does not copy surfaces through
 * AImageReader or use the EGL shader cache, so the stubs are never exercised.
 */

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaError.h>
#include <pipeline/skia/ShaderCache.h>
#include <SkData.h>
#include <SkString.h>

namespace android {

void egl_set_cache_filename(const char*) {}

namespace uirenderer {
namespace skiapipeline {

ShaderCache::ShaderCache() = default;

ShaderCache& ShaderCache::get() {
    static ShaderCache cache;
    return cache;
}

void ShaderCache::initShaderDiskCache(const void*, ssize_t) {}

void ShaderCache::setFilename(const char*) {}

sk_sp<SkData> ShaderCache::load(const SkData&) {
    return nullptr;
}

void ShaderCache::store(const SkData&, const SkData&, const SkString&) {}

void ShaderCache::onVkFrameFlushed(GrDirectContext*) {}

}  // namespace skiapipeline
}  // namespace uirenderer

}  // namespace android

extern "C" {

media_status_t AImageReader_newWithUsage(int32_t, int32_t, int32_t, uint64_t, int32_t,
                                         AImageReader** reader) {
    if (reader != nullptr) {
        *reader = nullptr;
    }
    return AMEDIA_ERROR_UNSUPPORTED;
}

void AImageReader_delete(AImageReader*) {}

media_status_t AImageReader_getWindow(AImageReader*, ANativeWindow** window) {
    if (window != nullptr) {
        *window = nullptr;
    }
    return AMEDIA_ERROR_UNSUPPORTED;
}

media_status_t AImageReader_acquireNextImage(AImageReader*, AImage** image) {
    if (image != nullptr) {
        *image = nullptr;
    }
    return AMEDIA_ERROR_UNSUPPORTED;
}

void AImage_delete(AImage*) {}

media_status_t AImage_getHardwareBuffer(const AImage*, AHardwareBuffer** buffer) {
    if (buffer != nullptr) {
        *buffer = nullptr;
    }
    return AMEDIA_ERROR_UNSUPPORTED;
}

}  // extern "C"
