/*
 * Host gralloc backend for droidlet.  See ui/GrallocHost.h for the design.
 *
 * Buffers are plain mmap-able fds: today a memfd, later a dma-buf.  The
 * native_handle_t metadata is versioned so the format can grow without
 * changing the mapper/composer contract.
 */

#define LOG_TAG "GrallocHost"

#include <ui/GrallocHost.h>

#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <log/log.h>

// We would eliminate the non-conforming zero-length array, but we can't since
// this is effectively included from the Linux kernel.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#include <sync/sync.h>
#pragma clang diagnostic pop

#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if __has_include(<linux/dma-buf.h>)
#include <linux/dma-buf.h>
#define DROIDLET_HAVE_DMA_BUF_H 1
#else
#define DROIDLET_HAVE_DMA_BUF_H 0
#endif

#if __has_include(<linux/dma-heap.h>)
#include <linux/dma-heap.h>
#define DROIDLET_HAVE_DMA_HEAP_H 1
#else
#define DROIDLET_HAVE_DMA_HEAP_H 0
#endif

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <ui/GraphicBufferMapper.h>

namespace android {
namespace {

// Align rows so a future dma-buf backend and any accelerated consumer see the
// same layout.  64 bytes covers RGBA8 and everything wider.
constexpr uint32_t kHostBufferStrideAlignment = 64;

constexpr int kHandleMagicIndex = 0;
constexpr int kHandleVersionIndex = 1;
constexpr int kHandleBackendIndex = 2;
constexpr int kHandleWidthIndex = 3;
constexpr int kHandleHeightIndex = 4;
constexpr int kHandleStrideIndex = 5;
constexpr int kHandleFormatIndex = 6;
constexpr int kHandleUsageLowIndex = 7;
constexpr int kHandleUsageHighIndex = 8;
constexpr int kHandleLayerCountIndex = 9;
constexpr int kHandleSizeLowIndex = 10;
constexpr int kHandleSizeHighIndex = 11;

int* handleInts(const native_handle_t* handle) {
    return const_cast<int*>(handle->data + handle->numFds);
}

// Ceil-divides width * bpp bytes up to the stride alignment, then converts
// back to pixels.
uint32_t computeStridePixels(uint32_t width, uint32_t bpp) {
    const uint64_t rowBytes = static_cast<uint64_t>(width) * bpp;
    const uint64_t aligned =
            (rowBytes + kHostBufferStrideAlignment - 1) / kHostBufferStrideAlignment *
            kHostBufferStrideAlignment;
    return static_cast<uint32_t>((aligned + bpp - 1) / bpp);
}

bool isLogicalPixelFormat(PixelFormat format) {
    return format == PIXEL_FORMAT_TRANSLUCENT || format == PIXEL_FORMAT_TRANSPARENT ||
            format == PIXEL_FORMAT_OPAQUE;
}

}  // namespace

// ---------------------------------------------------------------------------
// Handle helpers
// ---------------------------------------------------------------------------

status_t hostBufferCreateHandle(const HostBufferInfo& info, int fd,
                                native_handle_t** outHandle) {
    if (outHandle == nullptr) {
        return BAD_VALUE;
    }
    *outHandle = nullptr;
    if (fd < 0) {
        return BAD_VALUE;
    }

    native_handle_t* handle =
            native_handle_create(kHostBufferHandleNumFds, kHostBufferHandleNumInts);
    if (handle == nullptr) {
        close(fd);
        return NO_MEMORY;
    }

    handle->data[0] = fd;
    int* ints = handleInts(handle);
    ints[kHandleMagicIndex] = kHostBufferHandleMagic;
    ints[kHandleVersionIndex] = kHostBufferHandleVersion;
    ints[kHandleBackendIndex] = static_cast<int32_t>(info.backend);
    ints[kHandleWidthIndex] = static_cast<int32_t>(info.width);
    ints[kHandleHeightIndex] = static_cast<int32_t>(info.height);
    ints[kHandleStrideIndex] = static_cast<int32_t>(info.stride);
    ints[kHandleFormatIndex] = static_cast<int32_t>(info.format);
    ints[kHandleUsageLowIndex] = static_cast<int32_t>(info.usage & 0xFFFFFFFFull);
    ints[kHandleUsageHighIndex] = static_cast<int32_t>(info.usage >> 32);
    ints[kHandleLayerCountIndex] = static_cast<int32_t>(info.layerCount);
    ints[kHandleSizeLowIndex] = static_cast<int32_t>(info.size & 0xFFFFFFFFull);
    ints[kHandleSizeHighIndex] = static_cast<int32_t>(info.size >> 32);

    *outHandle = handle;
    return OK;
}

status_t hostBufferReadHandle(const native_handle_t* handle, HostBufferInfo* outInfo) {
    if (outInfo == nullptr) {
        return BAD_VALUE;
    }
    *outInfo = HostBufferInfo{};
    if (handle == nullptr || handle->numFds != kHostBufferHandleNumFds ||
        handle->numInts < kHostBufferHandleNumInts) {
        return BAD_TYPE;
    }

    const int* ints = handle->data + handle->numFds;
    if (ints[kHandleMagicIndex] != kHostBufferHandleMagic ||
        ints[kHandleVersionIndex] != kHostBufferHandleVersion) {
        return BAD_TYPE;
    }

    const int32_t backend = ints[kHandleBackendIndex];
    if (backend != static_cast<int32_t>(HostBufferBackend::Memfd) &&
        backend != static_cast<int32_t>(HostBufferBackend::DmaHeap)) {
        return BAD_TYPE;
    }

    outInfo->backend = static_cast<HostBufferBackend>(backend);
    outInfo->width = static_cast<uint32_t>(ints[kHandleWidthIndex]);
    outInfo->height = static_cast<uint32_t>(ints[kHandleHeightIndex]);
    outInfo->stride = static_cast<uint32_t>(ints[kHandleStrideIndex]);
    outInfo->format = static_cast<PixelFormat>(ints[kHandleFormatIndex]);
    outInfo->usage = (static_cast<uint64_t>(static_cast<uint32_t>(ints[kHandleUsageHighIndex]))
                      << 32) |
            static_cast<uint32_t>(ints[kHandleUsageLowIndex]);
    outInfo->layerCount = static_cast<uint32_t>(ints[kHandleLayerCountIndex]);
    outInfo->size = (static_cast<uint64_t>(static_cast<uint32_t>(ints[kHandleSizeHighIndex]))
                     << 32) |
            static_cast<uint32_t>(ints[kHandleSizeLowIndex]);

    if (outInfo->width == 0 || outInfo->height == 0 || outInfo->stride == 0 ||
        outInfo->layerCount == 0 || outInfo->size == 0) {
        return BAD_VALUE;
    }
    return OK;
}

bool hostBufferIsHostHandle(const native_handle_t* handle) {
    HostBufferInfo info;
    return hostBufferReadHandle(handle, &info) == OK;
}

int hostBufferDupFd(const native_handle_t* handle) {
    if (handle == nullptr || handle->numFds < 1) {
        return -1;
    }
    return fcntl(handle->data[0], F_DUPFD_CLOEXEC, 0);
}

status_t hostBufferBeginCpuAccess(HostBufferBackend backend, int fd) {
    switch (backend) {
        case HostBufferBackend::Memfd:
            // Coherent by construction; nothing to do.
            return OK;
        case HostBufferBackend::DmaHeap: {
#if DROIDLET_HAVE_DMA_BUF_H
            struct dma_buf_sync sync {};
            sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
            if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) != 0) {
                return -errno;
            }
            return OK;
#else
            (void)fd;
            return INVALID_OPERATION;
#endif
        }
    }
    return BAD_TYPE;
}

status_t hostBufferEndCpuAccess(HostBufferBackend backend, int fd) {
    switch (backend) {
        case HostBufferBackend::Memfd:
            return OK;
        case HostBufferBackend::DmaHeap: {
#if DROIDLET_HAVE_DMA_BUF_H
            struct dma_buf_sync sync {};
            sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
            if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) != 0) {
                return -errno;
            }
            return OK;
#else
            (void)fd;
            return INVALID_OPERATION;
#endif
        }
    }
    return BAD_TYPE;
}

// ---------------------------------------------------------------------------
// Backends
// ---------------------------------------------------------------------------

namespace {

class MemfdHostBufferFdAllocator final : public HostBufferFdAllocator {
public:
    const char* name() const override { return "memfd"; }

    int allocate(HostBufferInfo& info) override {
        int fd = memfd_create("droidlet-gralloc", MFD_CLOEXEC);
        if (fd < 0) {
            ALOGE("memfd_create failed: %s", strerror(errno));
            return -1;
        }
        if (ftruncate(fd, static_cast<off_t>(info.size)) != 0) {
            ALOGE("ftruncate(%" PRIu64 ") failed: %s", info.size, strerror(errno));
            close(fd);
            return -1;
        }

        // Fault every page in now.  A sparse memfd would SIGBUS on the composer
        // side if the host ran out of memory mid-frame, and zeroing keeps the
        // first frame deterministic.
        void* base = mmap(nullptr, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED) {
            ALOGE("mmap(%" PRIu64 ") failed: %s", info.size, strerror(errno));
            close(fd);
            return -1;
        }
        memset(base, 0, info.size);
        munmap(base, info.size);
        return fd;
    }
};

class DmaHeapHostBufferFdAllocator final : public HostBufferFdAllocator {
public:
    const char* name() const override { return "dmaheap"; }
    HostBufferBackend backend() const override { return HostBufferBackend::DmaHeap; }

    int allocate(HostBufferInfo& info) override {
        info.backend = HostBufferBackend::DmaHeap;
#if DROIDLET_HAVE_DMA_HEAP_H
        const char* envPath = std::getenv("DROIDLET_DMA_HEAP_PATH");
        const char* path = (envPath != nullptr && *envPath != '\0')
                ? envPath : "/dev/dma_heap/system";
        base::unique_fd heap(open(path, O_RDONLY | O_CLOEXEC));
        if (heap.get() < 0) {
            ALOGW("dma-heap %s unavailable (%s); falling back to memfd", path,
                  strerror(errno));
            return fallback(info);
        }

        struct dma_heap_allocation_data data {};
        data.len = info.size;
        data.fd_flags = O_RDWR | O_CLOEXEC;
        data.heap_flags = 0;
        if (ioctl(heap.get(), DMA_HEAP_IOCTL_ALLOC, &data) != 0) {
            ALOGW("DMA_HEAP_IOCTL_ALLOC(%" PRIu64 ") failed: %s; falling back to memfd",
                  info.size, strerror(errno));
            return fallback(info);
        }

        const int fd = static_cast<int>(data.fd);
        if (hostBufferBeginCpuAccess(HostBufferBackend::DmaHeap, fd) != OK) {
            ALOGW("dma-buf CPU sync failed; falling back to memfd");
            close(fd);
            return fallback(info);
        }
        void* base = mmap(nullptr, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED) {
            ALOGW("dma-buf mmap(%" PRIu64 ") failed: %s; falling back to memfd",
                  info.size, strerror(errno));
            close(fd);
            hostBufferEndCpuAccess(HostBufferBackend::DmaHeap, fd);
            return fallback(info);
        }
        // Keep the first frame deterministic, as the memfd backend does.
        memset(base, 0, info.size);
        munmap(base, info.size);
        hostBufferEndCpuAccess(HostBufferBackend::DmaHeap, fd);
        return fd;
#else
        ALOGW("host build has no <linux/dma-heap.h>; falling back to memfd");
        return fallback(info);
#endif
    }

private:
    int fallback(HostBufferInfo& info) {
        info.backend = HostBufferBackend::Memfd;
        return mMemfd.allocate(info);
    }

    MemfdHostBufferFdAllocator mMemfd;
};

}  // namespace

std::unique_ptr<HostBufferFdAllocator> createMemfdHostBufferFdAllocator() {
    return std::make_unique<MemfdHostBufferFdAllocator>();
}

std::unique_ptr<HostBufferFdAllocator> createHostBufferFdAllocator(const char* requested) {
    const char* env = std::getenv("DROIDLET_GRALLOC_BACKEND");
    if (requested == nullptr || *requested == '\0') {
        requested = env;
    }
    // Prefer dma-heap so AHardwareBuffers can be imported as EGLImages.
    // createHostBufferFdAllocator() falls back to memfd when the heap is
    // unavailable.
    const char* name = (requested != nullptr && *requested != '\0') ? requested : "dmaheap";

    if (strcmp(name, "memfd") == 0) {
        return createMemfdHostBufferFdAllocator();
    }
    if (strcmp(name, "dmaheap") == 0 || strcmp(name, "dma-buf") == 0) {
        return std::make_unique<DmaHeapHostBufferFdAllocator>();
    }
    ALOGW("host gralloc: unknown backend '%s'; using memfd", name);
    return createMemfdHostBufferFdAllocator();
}

// ---------------------------------------------------------------------------
// GrallocHostMapper
// ---------------------------------------------------------------------------

GrallocHostMapper::GrallocHostMapper() = default;
GrallocHostMapper::~GrallocHostMapper() = default;

status_t GrallocHostMapper::readHandle(buffer_handle_t handle, HostBufferInfo* outInfo) {
    status_t status = hostBufferReadHandle(handle, outInfo);
    if (status != OK) {
        ALOGE("invalid host gralloc handle %p", handle);
    }
    return status;
}

std::string GrallocHostMapper::dumpBuffer(buffer_handle_t bufferHandle, bool /*less*/) const {
    HostBufferInfo info;
    if (readHandle(bufferHandle, &info) != OK) {
        return "<invalid host buffer>\n";
    }
    return base::StringPrintf("host buffer %p: %ux%u stride=%u format=0x%x usage=0x%" PRIx64
                              " layers=%u size=%" PRIu64 " backend=%d\n",
                              bufferHandle, info.width, info.height, info.stride, info.format,
                              info.usage, info.layerCount, info.size,
                              static_cast<int>(info.backend));
}

status_t GrallocHostMapper::importBuffer(const native_handle_t* rawHandle,
                                         buffer_handle_t* outBufferHandle) const {
    if (outBufferHandle == nullptr) {
        return BAD_VALUE;
    }
    *outBufferHandle = nullptr;

    HostBufferInfo info;
    status_t status = readHandle(rawHandle, &info);
    if (status != OK) {
        return status;
    }

    native_handle_t* clone = native_handle_clone(rawHandle);
    if (clone == nullptr) {
        return NO_MEMORY;
    }
    *outBufferHandle = clone;
    return OK;
}

void GrallocHostMapper::freeBuffer(buffer_handle_t bufferHandle) const {
    if (bufferHandle == nullptr) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mMappingsMutex);
        auto it = mMappings.find(bufferHandle);
        if (it != mMappings.end()) {
            if (it->second.base != nullptr && it->second.size != 0) {
                munmap(it->second.base, it->second.size);
            }
            mMappings.erase(it);
        }
    }

    native_handle_close(bufferHandle);
    native_handle_delete(const_cast<native_handle_t*>(bufferHandle));
}

status_t GrallocHostMapper::validateBufferSize(buffer_handle_t bufferHandle, uint32_t width,
                                               uint32_t height, PixelFormat format,
                                               uint32_t layerCount, uint64_t /*usage*/,
                                               uint32_t stride) const {
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status != OK) {
        return status;
    }
    if (info.width != width || info.height != height || info.layerCount != layerCount) {
        ALOGE("buffer size mismatch: got %ux%u layers=%u, expected %ux%u layers=%u",
              info.width, info.height, info.layerCount, width, height, layerCount);
        return BAD_VALUE;
    }
    if (!isLogicalPixelFormat(format) && info.format != format) {
        ALOGE("buffer format mismatch: got 0x%x, expected 0x%x", info.format, format);
        return BAD_VALUE;
    }
    if (stride != 0 && info.stride != stride) {
        ALOGE("buffer stride mismatch: got %u, expected %u", info.stride, stride);
        return BAD_VALUE;
    }
    return OK;
}

void GrallocHostMapper::getTransportSize(buffer_handle_t /*bufferHandle*/, uint32_t* outNumFds,
                                         uint32_t* outNumInts) const {
    if (outNumFds != nullptr) {
        *outNumFds = kHostBufferHandleNumFds;
    }
    if (outNumInts != nullptr) {
        *outNumInts = kHostBufferHandleNumInts;
    }
}

status_t GrallocHostMapper::lock(buffer_handle_t bufferHandle, uint64_t /*usage*/,
                                 const Rect& /*bounds*/, int acquireFence, void** outData,
                                 int32_t* outBytesPerPixel, int32_t* outBytesPerStride) const {
    if (outData == nullptr) {
        if (acquireFence >= 0) {
            close(acquireFence);
        }
        return BAD_VALUE;
    }
    *outData = nullptr;

    // The mapper owns the fence in all cases.
    base::unique_fd fence(acquireFence);
    if (acquireFence >= 0) {
        int err = sync_wait(acquireFence, 1000);
        if (err < 0) {
            // Host buffers only ever carry -1 fences today, but a producer could
            // hand us a real one later.  Log and continue rather than failing a
            // frame that may already be done.
            ALOGW("acquire fence wait failed: %s", strerror(errno));
        }
    }

    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status != OK) {
        return status;
    }

    const uint32_t bpp = bytesPerPixel(info.format);
    if (bpp == 0) {
        ALOGE("unsupported host buffer format 0x%x", info.format);
        return BAD_VALUE;
    }

    status = hostBufferBeginCpuAccess(info.backend, bufferHandle->data[0]);
    if (status != OK) {
        return status;
    }

    std::lock_guard<std::mutex> lock(mMappingsMutex);
    auto it = mMappings.find(bufferHandle);
    if (it == mMappings.end()) {
        void* base = mmap(nullptr, info.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          bufferHandle->data[0], 0);
        if (base == MAP_FAILED) {
            status = -errno;
            ALOGE("mmap(%" PRIu64 ") failed: %s", info.size, strerror(errno));
            return status;
        }
        it = mMappings.emplace(bufferHandle, Mapping{base, info.size}).first;
    }

    *outData = it->second.base;
    if (outBytesPerPixel != nullptr) {
        *outBytesPerPixel = static_cast<int32_t>(bpp);
    }
    if (outBytesPerStride != nullptr) {
        *outBytesPerStride = static_cast<int32_t>(info.stride * bpp);
    }
    return OK;
}

status_t GrallocHostMapper::lock(buffer_handle_t bufferHandle, uint64_t /*usage*/,
                                 const Rect& /*bounds*/, int acquireFence,
                                 android_ycbcr* /*ycbcr*/) const {
    if (acquireFence >= 0) {
        close(acquireFence);
    }
    ALOGE("lockYCbCr is not supported by the host gralloc backend (%p)", bufferHandle);
    return INVALID_OPERATION;
}

int GrallocHostMapper::unlock(buffer_handle_t bufferHandle) const {
    HostBufferInfo info;
    if (readHandle(bufferHandle, &info) != OK) {
        return -1;
    }
    status_t status = hostBufferEndCpuAccess(info.backend, bufferHandle->data[0]);
    if (status != OK) {
        ALOGW("end CPU access failed: %d", status);
    }
    return -1;  // no release fence for host memory
}

status_t GrallocHostMapper::isSupported(uint32_t /*width*/, uint32_t /*height*/,
                                        PixelFormat /*format*/, uint32_t /*layerCount*/,
                                        uint64_t /*usage*/, bool* outSupported) const {
    if (outSupported != nullptr) {
        *outSupported = true;
    }
    return OK;
}

status_t GrallocHostMapper::getBufferId(buffer_handle_t /*bufferHandle*/,
                                        uint64_t* /*outBufferId*/) const {
    return INVALID_OPERATION;
}

status_t GrallocHostMapper::getName(buffer_handle_t /*bufferHandle*/,
                                    std::string* outName) const {
    if (outName != nullptr) {
        *outName = "host-gralloc";
    }
    return OK;
}

status_t GrallocHostMapper::getWidth(buffer_handle_t bufferHandle, uint64_t* outWidth) const {
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status == OK && outWidth != nullptr) {
        *outWidth = info.width;
    }
    return status;
}

status_t GrallocHostMapper::getHeight(buffer_handle_t bufferHandle, uint64_t* outHeight) const {
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status == OK && outHeight != nullptr) {
        *outHeight = info.height;
    }
    return status;
}

status_t GrallocHostMapper::getLayerCount(buffer_handle_t bufferHandle,
                                          uint64_t* outLayerCount) const {
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status == OK && outLayerCount != nullptr) {
        *outLayerCount = info.layerCount;
    }
    return status;
}

status_t GrallocHostMapper::getPixelFormatRequested(
        buffer_handle_t bufferHandle, ui::PixelFormat* outPixelFormatRequested) const {
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status == OK && outPixelFormatRequested != nullptr) {
        *outPixelFormatRequested = static_cast<ui::PixelFormat>(info.format);
    }
    return status;
}

status_t GrallocHostMapper::getPixelFormatFourCC(buffer_handle_t /*bufferHandle*/,
                                                 uint32_t* /*outPixelFormatFourCC*/) const {
    return INVALID_OPERATION;
}

status_t GrallocHostMapper::getPixelFormatModifier(buffer_handle_t /*bufferHandle*/,
                                                   uint64_t* /*outPixelFormatModifier*/) const {
    return INVALID_OPERATION;
}

status_t GrallocHostMapper::getUsage(buffer_handle_t bufferHandle, uint64_t* outUsage) const {
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status == OK && outUsage != nullptr) {
        *outUsage = info.usage;
    }
    return status;
}

status_t GrallocHostMapper::getAllocationSize(buffer_handle_t bufferHandle,
                                              uint64_t* outAllocationSize) const {
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status == OK && outAllocationSize != nullptr) {
        *outAllocationSize = info.size;
    }
    return status;
}

status_t GrallocHostMapper::getStride(buffer_handle_t bufferHandle, uint32_t* outStride) const {
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status == OK && outStride != nullptr) {
        *outStride = info.stride;
    }
    return status;
}

status_t GrallocHostMapper::getPlaneLayouts(
        buffer_handle_t bufferHandle, std::vector<ui::PlaneLayout>* outPlaneLayouts) const {
    if (outPlaneLayouts == nullptr) {
        return BAD_VALUE;
    }
    HostBufferInfo info;
    status_t status = readHandle(bufferHandle, &info);
    if (status != OK) {
        return status;
    }
    const uint32_t bpp = bytesPerPixel(info.format);
    if (bpp == 0) {
        return BAD_VALUE;
    }

    ui::PlaneLayout layout;
    layout.offsetInBytes = 0;
    // GraphicBuffer::lockAsync() derives the legacy bytesPerPixel from this
    // field, so it carries bits per pixel rather than bits per sample.
    layout.sampleIncrementInBits = static_cast<int64_t>(bpp) * 8;
    layout.strideInBytes = static_cast<int64_t>(info.stride) * bpp;
    layout.widthInSamples = info.width;
    layout.heightInSamples = info.height;
    layout.totalSizeInBytes = static_cast<int64_t>(info.size);
    layout.horizontalSubsampling = 1;
    layout.verticalSubsampling = 1;

    outPlaneLayouts->clear();
    outPlaneLayouts->push_back(std::move(layout));
    return OK;
}

// ---------------------------------------------------------------------------
// GrallocHostAllocator
// ---------------------------------------------------------------------------

GrallocHostAllocator::GrallocHostAllocator()
      : mBackend(createHostBufferFdAllocator(/*requested=*/nullptr)) {
    ALOGI("host gralloc allocator ready (backend=%s)",
          mBackend != nullptr ? mBackend->name() : "none");
}

GrallocHostAllocator::~GrallocHostAllocator() = default;

std::string GrallocHostAllocator::dumpDebugInfo(bool /*less*/) const {
    return base::StringPrintf("droidlet host gralloc backend: %s\n",
                              mBackend != nullptr ? mBackend->name() : "none");
}

status_t GrallocHostAllocator::allocate(std::string requestorName, uint32_t width,
                                        uint32_t height, PixelFormat format,
                                        uint32_t layerCount, uint64_t usage,
                                        uint32_t* outStride,
                                        buffer_handle_t* outBufferHandles,
                                        bool importBuffers) const {
    if (outStride == nullptr || outBufferHandles == nullptr) {
        return BAD_VALUE;
    }
    *outStride = 0;
    *outBufferHandles = nullptr;

    if (mBackend == nullptr) {
        return NO_INIT;
    }
    if (width == 0 || height == 0) {
        width = height = 1;
    }
    if (layerCount == 0) {
        layerCount = 1;
    }

    const uint32_t bpp = bytesPerPixel(format);
    if (bpp == 0) {
        ALOGE("unsupported host gralloc format 0x%x (requestor %s)", format,
              requestorName.c_str());
        return BAD_VALUE;
    }

    if (width > std::numeric_limits<uint32_t>::max() / bpp) {
        return BAD_VALUE;
    }
    const uint32_t stride = computeStridePixels(width, bpp);
    if (stride == 0) {
        return BAD_VALUE;
    }

    const uint64_t strideBytes = static_cast<uint64_t>(stride) * bpp;
    if (height > std::numeric_limits<uint64_t>::max() / strideBytes ||
        layerCount > std::numeric_limits<uint64_t>::max() / (strideBytes * height)) {
        ALOGE("host gralloc allocation too large (%ux%u layers=%u)", width, height, layerCount);
        return NO_MEMORY;
    }
    const uint64_t size = strideBytes * height * layerCount;

    HostBufferInfo info;
    info.width = width;
    info.height = height;
    info.stride = stride;
    info.format = format;
    info.usage = usage;
    info.layerCount = layerCount;
    info.size = size;
    info.backend = mBackend->backend();

    int fd = mBackend->allocate(info);
    if (fd < 0) {
        ALOGE("host gralloc allocation failed (%ux%u layers=%u format=0x%x usage=0x%" PRIx64
              ")",
              width, height, layerCount, format, usage);
        return NO_MEMORY;
    }

    native_handle_t* rawHandle = nullptr;
    // On success the handle owns the fd; on failure the fd has been closed.
    status_t status = hostBufferCreateHandle(info, fd, &rawHandle);
    if (status != OK) {
        return status;
    }

    if (importBuffers) {
        buffer_handle_t imported = nullptr;
        status = GraphicBufferMapper::get().importBuffer(rawHandle, width, height, layerCount,
                                                         format, usage, stride, &imported);
        native_handle_close(rawHandle);
        native_handle_delete(rawHandle);
        if (status != OK) {
            return status;
        }
        *outBufferHandles = imported;
    } else {
        *outBufferHandles = rawHandle;
    }

    *outStride = stride;
    ALOGV("allocated %ux%u stride=%u layers=%u format=0x%x size=%" PRIu64 " (%s)", width,
          height, stride, layerCount, format, size, requestorName.c_str());
    return OK;
}

GraphicBufferAllocator::AllocationResult GrallocHostAllocator::allocate(
        const GraphicBufferAllocator::AllocationRequest& request) const {
    uint32_t stride = 0;
    buffer_handle_t handle = nullptr;
    status_t status =
            allocate(request.requestorName, request.width, request.height, request.format,
                     request.layerCount, request.usage, &stride, &handle, request.importBuffer);
    if (status != OK) {
        return GraphicBufferAllocator::AllocationResult(status);
    }
    return GraphicBufferAllocator::AllocationResult(handle, stride);
}

}  // namespace android
