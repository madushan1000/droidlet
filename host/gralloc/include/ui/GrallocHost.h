/*
 * Host gralloc backend for droidlet.
 *
 * droidlet has no gralloc HAL, no hwservicemanager and no binder driver.  This
 * backend replaces the libui-internal GrallocMapper / GrallocAllocator
 * adapters (the layer that Gralloc4Mapper/Gralloc4Allocator normally implement
 * over the HIDL HAL) with plain host memory.
 *
 * The wire format is deliberately a normal native_handle_t carrying one fd and
 * a fixed metadata payload, so a buffer can be flattened through binder and
 * imported by the host composer exactly like a real gralloc buffer.  The fd is
 * an mmap-able, CPU-accessible buffer; today it is a memfd, later it can be a
 * dma-buf (dma-heap) without changing the handle layout, the mapper or the
 * composer.
 */

#pragma once

#include <cutils/native_handle.h>
#include <ui/Gralloc.h>
#include <ui/GraphicTypes.h>
#include <ui/PixelFormat.h>
#include <ui/Rect.h>
#include <utils/Errors.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace android {

// Where the backing fd came from.  The mapper treats every backend the same
// (mmap-able, CPU accessible); the tag only selects the cache-maintenance
// hooks around CPU access.
enum class HostBufferBackend : int32_t {
    Memfd = 0,    // memfd_create(MFD_CLOEXEC)
    DmaHeap = 1,  // /dev/dma_heap/* via DMA_HEAP_IOCTL_ALLOC
};

// Everything needed to describe (and later remap) a host buffer.
struct HostBufferInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;  // pixels
    PixelFormat format = PIXEL_FORMAT_UNKNOWN;
    uint64_t usage = 0;
    uint32_t layerCount = 1;
    uint64_t size = 0;  // bytes
    HostBufferBackend backend = HostBufferBackend::Memfd;
};

// Creates the backing fd for a host buffer.  A backend only deals with the fd;
// the native_handle_t layout lives in hostBufferCreateHandle().  Adding dma-buf
// support means adding one implementation here, not touching the mapper.
class HostBufferFdAllocator {
public:
    virtual ~HostBufferFdAllocator() = default;

    virtual const char* name() const = 0;

    // The backend tag stored in the handle.  An allocator that falls back to
    // another backend must report the backend it actually used.
    virtual HostBufferBackend backend() const { return HostBufferBackend::Memfd; }

    // Returns an owned fd, or -1 on failure.  May update info.backend when it
    // falls back to a different allocator.
    virtual int allocate(HostBufferInfo& info) = 0;
};

// The memfd backend, available on every host Linux kernel.
std::unique_ptr<HostBufferFdAllocator> createMemfdHostBufferFdAllocator();

// Selects a backend.  `requested` is usually DROIDLET_GRALLOC_BACKEND or
// nullptr; a missing/unavailable backend falls back to memfd with a warning.
std::unique_ptr<HostBufferFdAllocator> createHostBufferFdAllocator(
        const char* requested = nullptr);

// ---------------------------------------------------------------------------
// Handle layout.  Keep this in one place: the allocator, the mapper and the
// host composer all use these helpers.
//
//   ints[0]  magic ("DLGB")
//   ints[1]  version
//   ints[2]  backend (HostBufferBackend)
//   ints[3]  width
//   ints[4]  height
//   ints[5]  stride (pixels)
//   ints[6]  format (android PixelFormat)
//   ints[7]  usage low 32 bits
//   ints[8]  usage high 32 bits
//   ints[9]  layerCount
//   ints[10] size low 32 bits
//   ints[11] size high 32 bits
//   fds[0]   backing fd
// ---------------------------------------------------------------------------

constexpr int kHostBufferHandleMagic = 0x444C4742;  // "DLGB"
constexpr int kHostBufferHandleVersion = 1;
constexpr int kHostBufferHandleNumFds = 1;
constexpr int kHostBufferHandleNumInts = 12;

status_t hostBufferCreateHandle(const HostBufferInfo& info, int fd,
                                native_handle_t** outHandle);
status_t hostBufferReadHandle(const native_handle_t* handle, HostBufferInfo* outInfo);
bool hostBufferIsHostHandle(const native_handle_t* handle);

// Duplicates the handle's fd with CLOEXEC; returns -1 on failure.
int hostBufferDupFd(const native_handle_t* handle);

// CPU cache maintenance around a mapping.  memfd needs nothing; dma-buf heaps
// need DMA_BUF_IOCTL_SYNC.  Called by the mapper from lock()/unlock().
status_t hostBufferBeginCpuAccess(HostBufferBackend backend, int fd);
status_t hostBufferEndCpuAccess(HostBufferBackend backend, int fd);

// ---------------------------------------------------------------------------

class GrallocHostMapper final : public GrallocMapper {
public:
    GrallocHostMapper();
    ~GrallocHostMapper() override;

    bool isLoaded() const override { return true; }

    std::string dumpBuffer(buffer_handle_t bufferHandle, bool less) const override;

    status_t importBuffer(const native_handle_t* rawHandle,
                          buffer_handle_t* outBufferHandle) const override;
    void freeBuffer(buffer_handle_t bufferHandle) const override;
    status_t validateBufferSize(buffer_handle_t bufferHandle, uint32_t width, uint32_t height,
                                PixelFormat format, uint32_t layerCount, uint64_t usage,
                                uint32_t stride) const override;
    void getTransportSize(buffer_handle_t bufferHandle, uint32_t* outNumFds,
                          uint32_t* outNumInts) const override;

    status_t lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
                  int acquireFence, void** outData, int32_t* outBytesPerPixel,
                  int32_t* outBytesPerStride) const override;
    status_t lock(buffer_handle_t bufferHandle, uint64_t usage, const Rect& bounds,
                  int acquireFence, android_ycbcr* ycbcr) const override;
    int unlock(buffer_handle_t bufferHandle) const override;

    status_t isSupported(uint32_t width, uint32_t height, PixelFormat format,
                         uint32_t layerCount, uint64_t usage,
                         bool* outSupported) const override;

    status_t getBufferId(buffer_handle_t bufferHandle, uint64_t* outBufferId) const override;
    status_t getName(buffer_handle_t bufferHandle, std::string* outName) const override;
    status_t getWidth(buffer_handle_t bufferHandle, uint64_t* outWidth) const override;
    status_t getHeight(buffer_handle_t bufferHandle, uint64_t* outHeight) const override;
    status_t getLayerCount(buffer_handle_t bufferHandle, uint64_t* outLayerCount) const override;
    status_t getPixelFormatRequested(buffer_handle_t bufferHandle,
                                     ui::PixelFormat* outPixelFormatRequested) const override;
    status_t getPixelFormatFourCC(buffer_handle_t bufferHandle,
                                  uint32_t* outPixelFormatFourCC) const override;
    status_t getPixelFormatModifier(buffer_handle_t bufferHandle,
                                    uint64_t* outPixelFormatModifier) const override;
    status_t getUsage(buffer_handle_t bufferHandle, uint64_t* outUsage) const override;
    status_t getAllocationSize(buffer_handle_t bufferHandle,
                               uint64_t* outAllocationSize) const override;
    status_t getStride(buffer_handle_t bufferHandle, uint32_t* outStride) const override;
    status_t getPlaneLayouts(buffer_handle_t bufferHandle,
                             std::vector<ui::PlaneLayout>* outPlaneLayouts) const override;

private:
    struct Mapping {
        void* base = nullptr;
        uint64_t size = 0;
    };

    // Validates a handle and fills `info`; centralised so every entry point
    // reports the same error for non-host handles.
    static status_t readHandle(buffer_handle_t handle, HostBufferInfo* outInfo);

    mutable std::mutex mMappingsMutex;
    mutable std::unordered_map<buffer_handle_t, Mapping> mMappings;
};

class GrallocHostAllocator final : public GrallocAllocator {
public:
    GrallocHostAllocator();
    ~GrallocHostAllocator() override;

    bool isLoaded() const override { return mBackend != nullptr; }

    std::string dumpDebugInfo(bool less = true) const override;

    status_t allocate(std::string requestorName, uint32_t width, uint32_t height,
                      PixelFormat format, uint32_t layerCount, uint64_t usage,
                      uint32_t* outStride, buffer_handle_t* outBufferHandles,
                      bool importBuffers = true) const override;

    GraphicBufferAllocator::AllocationResult allocate(
            const GraphicBufferAllocator::AllocationRequest& request) const override;

private:
    std::unique_ptr<HostBufferFdAllocator> mBackend;
};

}  // namespace android
