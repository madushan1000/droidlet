/*
 * Host stub for the Android media NDK.
 *
 * There is no MediaCodec/MediaDrm service in the host runtime.  Fennec ships
 * libxul and libmozavcodec linked against libmediandk.so, so the symbols must
 * exist for the bionic loader to relocate them.  Every operation reports
 * AMEDIA_ERROR_UNSUPPORTED (or returns null); the app can then fall back to
 * software paths instead of failing to load its native libraries.
 */

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaCrypto.h>
#include <media/NdkMediaDrm.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include <cstddef>
#include <cstdint>

namespace {

constexpr media_status_t kUnsupported = AMEDIA_ERROR_UNSUPPORTED;
constexpr char kEmptyFormat[] = "{}";

}  // namespace

extern "C" {

/*
 * AMediaFormat is a small opaque value used by callers to describe codecs.
 * Return a shared dummy object so setters can be called safely even though
 * no codec will consume the result.
 */
AMediaFormat* AMediaFormat_new() {
    static void* dummy;
    return reinterpret_cast<AMediaFormat*>(&dummy);
}

media_status_t AMediaFormat_delete(AMediaFormat*) {
    return AMEDIA_OK;
}

const char* AMediaFormat_toString(AMediaFormat*) {
    return kEmptyFormat;
}

bool AMediaFormat_getInt32(AMediaFormat*, const char*, int32_t*) {
    return false;
}

bool AMediaFormat_getInt64(AMediaFormat*, const char*, int64_t*) {
    return false;
}

bool AMediaFormat_getFloat(AMediaFormat*, const char*, float*) {
    return false;
}

bool AMediaFormat_getBuffer(AMediaFormat*, const char*, void**, size_t*) {
    return false;
}

bool AMediaFormat_getString(AMediaFormat*, const char*, const char**) {
    return false;
}

void AMediaFormat_setInt32(AMediaFormat*, const char*, int32_t) {}

void AMediaFormat_setInt64(AMediaFormat*, const char*, int64_t) {}

void AMediaFormat_setFloat(AMediaFormat*, const char*, float) {}

void AMediaFormat_setString(AMediaFormat*, const char*, const char*) {}

void AMediaFormat_setBuffer(AMediaFormat*, const char*, const void*, size_t) {}

media_status_t AMediaFormat_copy(AMediaFormat*, AMediaFormat*) {
    return AMEDIA_OK;
}

AMediaCodec* AMediaCodec_createCodecByName(const char*) {
    return nullptr;
}

AMediaCodec* AMediaCodec_createDecoderByType(const char*) {
    return nullptr;
}

AMediaCodec* AMediaCodec_createEncoderByType(const char*) {
    return nullptr;
}

media_status_t AMediaCodec_delete(AMediaCodec*) {
    return AMEDIA_OK;
}

media_status_t AMediaCodec_configure(AMediaCodec*, const AMediaFormat*, ANativeWindow*,
        AMediaCrypto*, uint32_t) {
    return kUnsupported;
}

media_status_t AMediaCodec_start(AMediaCodec*) {
    return kUnsupported;
}

media_status_t AMediaCodec_stop(AMediaCodec*) {
    return kUnsupported;
}

media_status_t AMediaCodec_flush(AMediaCodec*) {
    return kUnsupported;
}

uint8_t* AMediaCodec_getInputBuffer(AMediaCodec*, size_t, size_t*) {
    return nullptr;
}

uint8_t* AMediaCodec_getOutputBuffer(AMediaCodec*, size_t, size_t*) {
    return nullptr;
}

ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec*, int64_t) {
    return kUnsupported;
}

media_status_t AMediaCodec_queueInputBuffer(AMediaCodec*, size_t, off_t, size_t,
        uint64_t, uint32_t) {
    return kUnsupported;
}

media_status_t AMediaCodec_queueSecureInputBuffer(AMediaCodec*, size_t, off_t,
        AMediaCodecCryptoInfo*, uint64_t, uint32_t) {
    return kUnsupported;
}

ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec*, AMediaCodecBufferInfo*, int64_t) {
    return kUnsupported;
}

AMediaFormat* AMediaCodec_getOutputFormat(AMediaCodec*) {
    return nullptr;
}

media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec*, size_t, bool) {
    return kUnsupported;
}

media_status_t AMediaCodec_releaseOutputBufferAtTime(AMediaCodec*, size_t, int64_t) {
    return kUnsupported;
}

media_status_t AMediaCodec_setParameters(AMediaCodec*, const AMediaFormat*) {
    return kUnsupported;
}

AMediaCodecCryptoInfo* AMediaCodecCryptoInfo_new(int, uint8_t[16], uint8_t[16],
        cryptoinfo_mode_t, size_t*, size_t*) {
    return nullptr;
}

media_status_t AMediaCodecCryptoInfo_delete(AMediaCodecCryptoInfo*) {
    return AMEDIA_OK;
}

void AMediaCodecCryptoInfo_setPattern(AMediaCodecCryptoInfo*, cryptoinfo_pattern_t*) {}

bool AMediaCrypto_isCryptoSchemeSupported(const AMediaUUID) {
    return false;
}

bool AMediaCrypto_requiresSecureDecoderComponent(const char*) {
    return false;
}

AMediaCrypto* AMediaCrypto_new(const AMediaUUID, const void*, size_t) {
    return nullptr;
}

void AMediaCrypto_delete(AMediaCrypto*) {}

bool AMediaDrm_isCryptoSchemeSupported(const uint8_t*, const char*) {
    return false;
}

AMediaDrm* AMediaDrm_createByUUID(const uint8_t*) {
    return nullptr;
}

void AMediaDrm_release(AMediaDrm*) {}

media_status_t AMediaDrm_setOnEventListener(AMediaDrm*, AMediaDrmEventListener) {
    return kUnsupported;
}

media_status_t AMediaDrm_openSession(AMediaDrm*, AMediaDrmSessionId*) {
    return kUnsupported;
}

media_status_t AMediaDrm_closeSession(AMediaDrm*, const AMediaDrmSessionId*) {
    return kUnsupported;
}

media_status_t AMediaDrm_getKeyRequest(AMediaDrm*, const AMediaDrmScope*,
        const uint8_t*, size_t, const char*, AMediaDrmKeyType,
        const AMediaDrmKeyValue*, size_t, const uint8_t**, size_t*) {
    return kUnsupported;
}

media_status_t AMediaDrm_provideKeyResponse(AMediaDrm*, const AMediaDrmScope*,
        const uint8_t*, size_t, AMediaDrmKeySetId*) {
    return kUnsupported;
}

media_status_t AMediaDrm_getProvisionRequest(AMediaDrm*, const uint8_t**, size_t*,
        const char**) {
    return kUnsupported;
}

media_status_t AMediaDrm_provideProvisionResponse(AMediaDrm*, const uint8_t*, size_t) {
    return kUnsupported;
}

media_status_t AMediaDrm_setPropertyString(AMediaDrm*, const char*, const char*) {
    return kUnsupported;
}

media_status_t AMediaDrm_setPropertyByteArray(AMediaDrm*, const char*, const uint8_t*,
        size_t) {
    return kUnsupported;
}

media_status_t AImageReader_acquireNextImageAsync(AImageReader*, AImage**, int*) {
    return kUnsupported;
}

media_status_t AImageReader_setImageListener(AImageReader*, AImageReader_ImageListener*) {
    return kUnsupported;
}

void AImage_deleteAsync(AImage*, int) {}

}  // extern "C"
