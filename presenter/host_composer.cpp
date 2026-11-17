/*
 * In-process SurfaceFlinger stand-in for droidlet.  See host_composer.h.
 *
 * The composer only implements the parts of the graphics stack that the
 * software rendering path uses:
 *
 *   Transaction::apply()
 *     -> legacy ISurfaceComposer::setTransactionState()        (direct call)
 *        -> worker thread: latch buffers, compose, present
 *
 *   SurfaceComposerClient::onFirstRef()
 *     -> AIDL createConnection()                               (direct call)
 *        -> SurfaceComposerClient::createSurfaceChecked()
 *           -> gui::ISurfaceComposerClient::createSurface()
 *
 * Everything is in-process, so the "remote" calls are virtual calls on the
 * local objects.  The AIDL composer is not a BnSurfaceComposer (that would
 * require implementing ~100 methods); it is an ISurfaceComposerDefault
 * implementation exposed through a plain BBinder whose queryLocalInterface()
 * returns it.  That is exactly what interface_cast<>() looks for.
 */

#define LOG_TAG "HostComposer"

#include "host_composer.h"

#include "perf_overlay.h"

#include "input_injector.h"
#include "presenter.h"
#include "wayland_host.h"

#include <android/gui/CreateSurfaceResult.h>
#include <android/gui/FrameStats.h>
#include <android/gui/ISurfaceComposer.h>
#include <android/gui/ISurfaceComposerClient.h>
#include <android/gui/SchedulingPolicy.h>
#include <android_runtime/AndroidRuntime.h>
#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <gui/ISurfaceComposer.h>
#include <gui/ITransactionCompletedListener.h>
#include <gui/LayerState.h>
#include <gui/Surface.h>
#include <gui/TransactionState.h>
#include <ui/FloatRect.h>
#include <ui/Fence.h>
#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>
#include <ui/Rect.h>
#include <utils/Log.h>
#include <utils/String16.h>
#include <utils/Timers.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace droidlet {
namespace {

using namespace android;

// Matches the value we hand to BLASTBufferQueue::initialize() through the AIDL
// composer.  BufferQueue has 64 slots, so this is comfortably within range.
constexpr uint32_t kMaxAcquiredBuffers = 3;

constexpr int kDefaultFrameIntervalMs = 16;

int frameIntervalMs() {
    const char* value = std::getenv("DROIDLET_FRAME_INTERVAL_MS");
    if (value == nullptr || *value == '\0') {
        return kDefaultFrameIntervalMs;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return kDefaultFrameIntervalMs;
    }
    return static_cast<int>(std::clamp(parsed, 1L, 1000L));
}

thread_local bool gWorkerAttached = false;

// The composer worker is a plain pthread, not a JVM thread.  Transaction
// listeners registered from Java (ViewRootImpl adds one to its BLAST sync
// transactions) run on whichever thread calls them, exactly like a binder
// callback from the real SurfaceFlinger.  Attach the worker lazily, once the
// VM exists, so those listeners see a valid JNIEnv.
bool ensureWorkerAttached() {
    if (gWorkerAttached) {
        return true;
    }
    JavaVM* vm = android::AndroidRuntime::getJavaVM();
    if (vm == nullptr) {
        return false;
    }
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm->AttachCurrentThreadAsDaemon(&env, nullptr) != JNI_OK) {
            ALOGE("unable to attach composer worker to the JVM");
            return false;
        }
    }
    gWorkerAttached = true;
    return true;
}

void detachWorker() {
    if (!gWorkerAttached) {
        return;
    }
    JavaVM* vm = android::AndroidRuntime::getJavaVM();
    if (vm != nullptr) {
        vm->DetachCurrentThread();
    }
    gWorkerAttached = false;
}

struct Layer {
    sp<IBinder> handle;
    int32_t id = -1;
    std::string name;
    sp<IBinder> parent;

    // Geometry in parent coordinates.
    float x = 0.0f;
    float y = 0.0f;
    int32_t z = 0;
    Rect destinationFrame;
    FloatRect crop;

    uint32_t flags = 0;
    uint32_t mask = 0;
    bool isContainer = false;

    // Composition state.  The producer-facing buffer stays untouched; these
    // are applied while copying pixels into the display frame.
    float alpha = 1.0f;
    bool hasColor = false;
    uint32_t color = 0xFF000000u;
    float matrix[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // dsdx, dtdx, dtdy, dsdy
    Rect bufferCrop;
    bool hasBufferCrop = false;
    gui::CornerRadii cornerRadii;
    bool hasCornerRadii = false;

    sp<GraphicBuffer> buffer;
    uint64_t frameNumber = 0;
    sp<Fence> acquireFence;
    sp<ITransactionCompletedListener> releaseListener;
    ReleaseCallbackId releaseId;
    bool hasBuffer = false;
    bool shared = false;
};

struct PendingFrame {
    std::vector<ListenerStats> listenerStats;
};

// ---------------------------------------------------------------------------
// AIDL composer client: creates layers and gives out their handles.
// ---------------------------------------------------------------------------

class HostSurfaceComposer;

// ---------------------------------------------------------------------------
// The composer itself.
// ---------------------------------------------------------------------------

class HostComposer {
public:
    HostComposer(int width, int height) : mWidth(width), mHeight(height) {}
    ~HostComposer() { shutdown(); }

    bool init(const char* backend) {
        mPresenter.reset(createPresenter(backend, mWidth, mHeight, "droidlet"));
        if (!mPresenter) {
            return false;
        }
        mFrame.assign(static_cast<size_t>(mWidth) * mHeight, 0xFF000000u);
        mFpsEnabled = std::getenv("DROIDLET_FPS") != nullptr;
        mTraceFrames = std::getenv("DROIDLET_TRACE_FRAMES") != nullptr;
        mDumpLayers = std::getenv("DROIDLET_DUMP_LAYERS") != nullptr;
        mFpsStart = std::chrono::steady_clock::now();
        mPerfOverlay.init(mPresenter->name(), mWidth, mHeight);
        mPresenterEffect = mPresenter->supportsEffect();
        if (mPresenterEffect) {
            mEffectFrame.assign(static_cast<size_t>(mWidth) * mHeight, 0);
        }
        mPresenterOverlay = mPresenter->supportsOverlay();
        if (std::getenv("DROIDLET_SCREENSHOT_MODE") != nullptr) {
            mPresenterOverlay = false;
        }
        if (mPresenterOverlay) {
            mOverlayFrame.assign(static_cast<size_t>(kOverlayWidth) * kOverlayHeight, 0);
        }
        mWorker = std::thread([this] { workerMain(); });
        ALOGI("started %dx%d composer with %s presenter", mWidth, mHeight, mPresenter->name());
        return true;
    }

    void shutdown() {
        {
            std::lock_guard lock(mQueueMutex);
            if (mStop) {
                return;
            }
            mStop = true;
        }
        mQueueCv.notify_all();
        if (mWorker.joinable()) {
            mWorker.join();
        }
        mPresenter.reset();
    }

    sp<IBinder> addLayer(const std::string& name, int32_t flags, const sp<IBinder>& parent,
                         int32_t* outId) {
        auto layer = std::make_shared<Layer>();
        layer->handle = sp<BBinder>::make();
        layer->name = name;
        layer->parent = parent;
        // SurfaceView creates hidden container layers and relies on a later
        // visibility transaction to show them.  Our window-session path does
        // not issue that transaction, so containers stay logically visible;
        // only buffer/effect layers are hidden by eLayerHidden.
        layer->isContainer =
                (flags & gui::ISurfaceComposerClient::eFXSurfaceMask) ==
                gui::ISurfaceComposerClient::eFXSurfaceContainer;
        if (flags & gui::ISurfaceComposerClient::eHidden) {
            layer->flags |= layer_state_t::eLayerHidden;
        }

        std::lock_guard lock(mLayerMutex);
        layer->id = mNextLayerId++;
        if (outId != nullptr) {
            *outId = layer->id;
        }
        mLayers[layer->id] = layer;
        mByHandle[layer->handle.get()] = layer;
        ALOGV("created layer %d '%s' flags=0x%x", layer->id, name.c_str(), flags);
        return layer->handle;
    }

    bool layerPosition(int layerId, float* x, float* y) const {
        std::lock_guard lock(mLayerMutex);
        auto it = mLayers.find(layerId);
        if (it == mLayers.end()) {
            return false;
        }
        if (x != nullptr) {
            *x = it->second->x;
        }
        if (y != nullptr) {
            *y = it->second->y;
        }
        return true;
    }

    static bool isShareable(const Layer& layer) {
        const float m00 = layer.matrix[0];
        const float m01 = layer.matrix[1];
        const float m10 = layer.matrix[2];
        const float m11 = layer.matrix[3];
        const bool identity = std::abs(m00 - 1.0f) < 1e-6f && std::abs(m01) < 1e-6f &&
                std::abs(m10) < 1e-6f && std::abs(m11 - 1.0f) < 1e-6f;
        return !layer.hasColor && !layer.hasCornerRadii && !layer.hasBufferCrop &&
                layer.alpha >= 0.999f && identity;
    }

    bool layerInfoForWindow(ANativeWindow* window, int* x, int* y, int* width, int* height,
                            int* z, float* alpha, bool* shareable) {
        if (window == nullptr) {
            return false;
        }
        waitForPendingTransactions();
        sp<IBinder> handle = Surface::getSurfaceControlHandle(window);
        if (handle == nullptr) {
            return false;
        }
        std::lock_guard lock(mLayerMutex);
        auto it = mByHandle.find(handle.get());
        if (it == mByHandle.end()) {
            return false;
        }
        const Layer& layer = *it->second;
        if (x != nullptr) {
            *x = layerOriginX(layer);
        }
        if (y != nullptr) {
            *y = layerOriginY(layer);
        }
        if (width != nullptr) {
            *width = layer.destinationFrame.isEmpty() ? 0 : layer.destinationFrame.getWidth();
        }
        if (height != nullptr) {
            *height = layer.destinationFrame.isEmpty() ? 0 : layer.destinationFrame.getHeight();
        }
        if (z != nullptr) {
            *z = layer.z;
        }
        if (alpha != nullptr) {
            *alpha = layer.alpha;
        }
        if (shareable != nullptr) {
            *shareable = isShareable(layer);
        }
        if (std::getenv("DROIDLET_COMPOSER_DEBUG") != nullptr) {
            static auto lastLayerLog = std::chrono::steady_clock::time_point{};
            const auto now = std::chrono::steady_clock::now();
            if (now - lastLayerLog >= std::chrono::seconds(1)) {
                lastLayerLog = now;
                ALOGI("layerInfo id=%d z=%d alpha=%.2f color=%d radii=%d crop=%d "
                      "matrix=[%.3f %.3f %.3f %.3f] shareable=%d dest=%dx%d",
                      layer.id, layer.z, layer.alpha, layer.hasColor ? 1 : 0,
                      layer.hasCornerRadii ? 1 : 0, layer.hasBufferCrop ? 1 : 0,
                      layer.matrix[0], layer.matrix[1], layer.matrix[2],
                      layer.matrix[3],
                      shareable != nullptr ? (*shareable ? 1 : 0) : -1,
                      layer.destinationFrame.getWidth(),
                      layer.destinationFrame.getHeight());
            }
        }
        return true;
    }

    void setLayerShared(ANativeWindow* window, bool shared) {
        if (window == nullptr) {
            return;
        }
        sp<IBinder> handle = Surface::getSurfaceControlHandle(window);
        if (handle == nullptr) {
            return;
        }
        std::lock_guard lock(mLayerMutex);
        auto it = mByHandle.find(handle.get());
        if (it != mByHandle.end()) {
            it->second->shared = shared;
        }
    }

    void removeLayer(int layerId) {
        {
            std::lock_guard lock(mLayerMutex);
            auto it = mLayers.find(layerId);
            if (it == mLayers.end()) {
                return;
            }
            mByHandle.erase(it->second->handle.get());
            mLayers.erase(it);
            mRegistryDirty.store(true, std::memory_order_relaxed);
            ALOGV("removed layer %d", layerId);
        }
        mQueueCv.notify_all();
    }

    void enqueue(TransactionState&& state) {
        {
            std::lock_guard lock(mQueueMutex);
            ALOGV("transaction %" PRIu64 ": %zu layers, %zu uncached, flags=0x%x",
                  state.mId, state.mComposerStates.size(), state.mUncacheBuffers.size(),
                  state.mFlags);
            mPendingTransactions.fetch_add(1, std::memory_order_release);
            mQueue.push_back(std::make_unique<TransactionState>(std::move(state)));
        }
        mQueueCv.notify_one();
    }

    /*
     * hwui creates the shared Wayland subsurface for a window as soon as it
     * draws the first frame, and reads the layer position at that moment.
     * The position transaction from relayout2 is applied by this composer's
     * worker, so a freshly added popup can still be at (0,0) with z=0 when
     * the subsurface is created; the popup then stays at the top-left corner
     * because a static window never presents again.  Wait for the queued
     * transactions to land before reporting layer geometry.
     */
    void waitForPendingTransactions() {
        if (mPendingTransactions.load(std::memory_order_acquire) == 0) {
            return;
        }
        std::unique_lock lock(mQueueMutex);
        mTransactionsAppliedCv.wait_for(lock, std::chrono::milliseconds(100),
                [this] { return mPendingTransactions.load(std::memory_order_acquire) == 0; });
    }

    // -- published frames (Java screenshot path) ----------------------------

    /*
     * Resize the staging frame to a new logical display size.  The presenter
     * resizes its own surfaces in the same configure callback; this only
     * updates the composition buffers and forces the next parent frame to be
     * presented.
     */
    void resize(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        std::lock_guard frameLock(mFrameMutex);
        if (width == mWidth && height == mHeight) {
            return;
        }
        if (!mPresenter->resize(width, height)) {
            ALOGW("presenter %s could not resize its surface to %dx%d",
                  mPresenter->name(), width, height);
        }
        mWidth = width;
        mHeight = height;
        mFrame.assign(static_cast<size_t>(width) * height, 0xFF000000u);
        mEffectFrame.assign(static_cast<size_t>(width) * height, 0);
        mParentHash = 0;
        mLastParentHash = ~0ull;
        mEffectBounds = Rect();
        mEffectActive = false;
        mEffectWasActive = false;
        ALOGI("composer resized to %dx%d", width, height);
    }

    bool waitForFrame(int timeoutMs) {
        std::unique_lock lock(mFrameMutex);
        return mFrameCv.wait_for(lock, std::chrono::milliseconds(std::max(timeoutMs, 0)),
                                 [this] { return mFrameSequence > 0; });
    }

    int copyFrame(uint32_t* out, int capacity) const {
        if (out == nullptr || capacity <= 0) {
            return -1;
        }
        const size_t total = static_cast<size_t>(mWidth) * mHeight;
        const size_t count = std::min(total, static_cast<size_t>(capacity));
        std::lock_guard lock(mFrameMutex);
        std::memcpy(out, mFrame.data(), count * sizeof(uint32_t));
        return static_cast<int>(count);
    }

    int frameWidth() const { return mWidth; }
    int frameHeight() const { return mHeight; }

    void frameStats(uint64_t* latched, uint64_t* presented, uint64_t* dropped) const {
        if (latched != nullptr) {
            *latched = mLatchedBuffers.load(std::memory_order_relaxed);
        }
        if (presented != nullptr) {
            *presented = mPresentedFrames.load(std::memory_order_relaxed);
        }
        if (dropped != nullptr) {
            *dropped = mDroppedBuffers.load(std::memory_order_relaxed);
        }
    }

private:
    // -- worker -------------------------------------------------------------

    /*
     * Test hook: simulate an xdg_toplevel configure from the compositor.  The
     * real path is the Wayland presenter's configure handler; this lets the
     * resize pipeline be exercised under compositors that cannot drag-resize
     * (weston has no server-side decorations).
     */
    void maybeTestResize() {
        const char* target = std::getenv("DROIDLET_RESIZE_TO");
        if (target == nullptr || target[0] == '\0') {
            return;
        }
        static bool done = false;
        static const auto start = std::chrono::steady_clock::now();
        if (done) {
            return;
        }
        const char* delayValue = std::getenv("DROIDLET_RESIZE_AFTER_MS");
        long delayMs = 2000;
        if (delayValue != nullptr && delayValue[0] != '\0') {
            char* end = nullptr;
            const long parsed = std::strtol(delayValue, &end, 10);
            if (end != delayValue && *end == '\0' && parsed >= 0) {
                delayMs = parsed;
            }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
        if (elapsed < delayMs) {
            return;
        }
        done = true;
        int width = 0;
        int height = 0;
        char extra = 0;
        if (sscanf(target, "%dx%d%c", &width, &height, &extra) == 2 &&
            width > 0 && height > 0) {
            ALOGI("resize test: %dx%d", width, height);
            hostResizeDisplay(width, height);
        }
    }

    void workerMain() {
        const auto interval = std::chrono::milliseconds(frameIntervalMs());
        auto nextPresent = std::chrono::steady_clock::now();

        for (;;) {
            std::vector<std::unique_ptr<TransactionState>> batch;
            {
                std::unique_lock lock(mQueueMutex);
                mQueueCv.wait_until(lock, nextPresent,
                                    [this] { return mStop || !mQueue.empty(); });
                if (mStop && mQueue.empty()) {
                    break;
                }
                batch.swap(mQueue);
            }

            bool dirty = mRegistryDirty.exchange(false, std::memory_order_relaxed);
            const size_t applied = batch.size();
            for (auto& state : batch) {
                dirty |= applyTransaction(*state);
                mPending.push_back(std::move(state));
            }
            if (applied > 0) {
                std::lock_guard lock(mQueueMutex);
                mPendingTransactions.fetch_sub(applied, std::memory_order_release);
                mTransactionsAppliedCv.notify_all();
            }

            mPresenter->poll();
            maybeTestResize();
            pumpInputResponses();
            pumpInputScript();

            if (!dirty && mPending.empty() && !mPerfOverlay.enabled()) {
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now < nextPresent) {
                // Coalesce transactions that arrived while the previous frame
                // was being composed.  The layer state is already applied, so
                // waiting only delays presentation, not updates.
                std::this_thread::sleep_until(nextPresent);
            }

            const auto composeStart = std::chrono::steady_clock::now();
            {
                std::lock_guard frameLock(mFrameMutex);
                composeFrame();
            }
            const double composeMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - composeStart).count();
            double effectMs = 0;
            if (mPresenterEffect && (mEffectActive || mEffectWasActive)) {
                const Rect bounds = mEffectActive ? mEffectBounds : mLastEffectBounds;
                int effectX = 0;
                int effectY = 0;
                int effectWidth = mWidth;
                int effectHeight = mHeight;
                if (!bounds.isEmpty()) {
                    effectX = bounds.left;
                    effectY = bounds.top;
                    effectWidth = bounds.getWidth();
                    effectHeight = bounds.getHeight();
                }
                const auto effectStart = std::chrono::steady_clock::now();
                const bool effectPresented = mPresenter->presentEffect(
                        mEffectFrame.data(), mWidth, mHeight, effectX, effectY, effectWidth,
                        effectHeight, mEffectZ);
                effectMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - effectStart).count();
                if (effectPresented) {
                    ++mEffectPresented;
                    if (mEffectActive) {
                        mLastEffectBounds = bounds;
                    }
                } else {
                    ++mEffectDropped;
                }
                if (mFpsEnabled) {
                    const auto now = std::chrono::steady_clock::now();
                    static auto lastEffectLog = now;
                    if (now - lastEffectLog >= std::chrono::seconds(1)) {
                        lastEffectLog = now;
                        ALOGI("effect presented=%" PRIu64 " dropped=%" PRIu64,
                              mEffectPresented.load(), mEffectDropped.load());
                    }
                }
                mEffectWasActive = mEffectActive;
            }
            if (!mEffectActive) {
                mLastEffectBounds = Rect();
            }
            if (mPresenterOverlay && mPerfOverlay.enabled() && mPerfOverlay.hasData()) {
                mPresenter->presentOverlay(mOverlayFrame.data(), kOverlayWidth, kOverlayHeight);
            }
            // The parent frame sits below the opaque shared app surface while
            // zero-copy rendering is active.  Re-uploading it every video
            // frame doubles the shm traffic for nothing; present it only when
            // its own content changed.
            const bool needParent = !mSharedMode || mParentHash != mLastParentHash;
            const auto presentStart = std::chrono::steady_clock::now();
            bool presented = true;
            double presentMs = 0;
            if (needParent) {
                presented = mPresenter->present(mFrame.data(), mWidth, mHeight);
                presentMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - presentStart).count();
                if (presented) {
                    mLastParentHash = mParentHash;
                }
            }
            if (presented) {
                const uint64_t latched = mLatchedBuffers.load(std::memory_order_relaxed);
                const uint64_t dropped = mDroppedBuffers.load(std::memory_order_relaxed);
                mPerfOverlay.onPresent(true, composeMs, presentMs, latched, dropped,
                                       mLastLayerCount, frameIntervalMs());
                if (mSharedMode) {
                    mPerfOverlay.onSharedFrames(waylandHostSwapCount());
                }
                {
                    std::lock_guard frameLock(mFrameMutex);
                    ++mFrameSequence;
                }
                mFrameCv.notify_all();
                const uint64_t presentedCount =
                        mPresentedFrames.load(std::memory_order_relaxed);
                if (presentedCount % 30 == 0 || latched != mLastDumpLatch) {
                    mLastDumpLatch = latched;
                    dumpFrame();
                }
                if (presentedCount == 0) {
                    ALOGI("presented the first composed frame at %dx%d", mWidth, mHeight);
                } else if (presentedCount % 300 == 0) {
                    ALOGV("presented frame %" PRIu64, presentedCount);
                }
                if (mTraceFrames) {
                    ALOGI("frame presented=%" PRIu64 " latched=%" PRIu64
                          " dropped=%" PRIu64,
                          presentedCount + 1, latched,
                          mDroppedBuffers.load(std::memory_order_relaxed));
                }
                if (mDumpLayers && presentedCount % 30 == 0) {
                    dumpLayers();
                }
                if (mFpsEnabled) {
                    ++mFpsFrames;
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - mFpsStart).count();
                    if (elapsed >= 1000) {
                        ALOGI("fps=%.1f (%" PRIu64 " frames in %lld ms) compose=%.1f"
                              " present=%.1f effect=%.1f",
                              mFpsFrames * 1000.0 / static_cast<double>(elapsed),
                              mFpsFrames, static_cast<long long>(elapsed), composeMs,
                              presentMs, effectMs);
                        mFpsFrames = 0;
                        mFpsStart = now;
                    }
                }
                ++mPresentedFrames;
            }
            {
                // The composed pixels have already been copied out of the
                // layer buffers, so the producers can get their buffers back
                // even when the presenter had no free shm buffer this tick.
                std::vector<std::unique_ptr<TransactionState>> presentedTransactions;
                presentedTransactions.swap(mPending);
                fireCallbacks(presentedTransactions);
            }
            nextPresent = std::chrono::steady_clock::now() + interval;
        }
        detachWorker();
    }

    // Applies one transaction to the layer registry.  Returns true when the
    // composed image may have changed.
    bool applyTransaction(const TransactionState& state) {
        bool dirty = false;
        std::vector<ListenerStats> callbacks;
        std::vector<const IBinder*> removedLayers;

        {
            std::lock_guard lock(mLayerMutex);
            for (const auto& composerState : state.mComposerStates) {
                const layer_state_t& s = composerState.state;
                auto it = mByHandle.find(s.surface.get());
                if (it == mByHandle.end()) {
                    ALOGW("transaction for unknown layer %d", s.layerId);
                    continue;
                }
                std::shared_ptr<Layer> layer = it->second;
                applyLayerState(*layer, s);
                // SurfaceControl.Transaction.remove() reparents to null
                // before releasing the handle.  Stop composing the layer so a
                // dismissed menu/dialog does not leave stale pixels on top.
                if ((s.what & layer_state_t::eReparent) &&
                    s.getParentSurfaceControlForChild() == nullptr) {
                    ALOGV("transaction: layer %d reparented to null", layer->id);
                    removedLayers.push_back(s.surface.get());
                }
                dirty = true;
            }

            for (const auto& uncache : state.mUncacheBuffers) {
                mBufferCache.erase(uncache.id);
            }

            callbacks = collectCallbacksLocked(state);

            for (const IBinder* handle : removedLayers) {
                auto it = mByHandle.find(handle);
                if (it != mByHandle.end()) {
                    ALOGV("transaction: removing layer %d", it->second->id);
                    mLayers.erase(it->second->id);
                    mByHandle.erase(it);
                    dirty = true;
                }
            }
        }

        if (!callbacks.empty()) {
            mPendingCallbacks.push_back(PendingFrame{std::move(callbacks)});
        }
        return dirty;
    }

    void applyLayerState(Layer& layer, const layer_state_t& s) {
        if (s.what & layer_state_t::eFlagsChanged) {
            layer.flags = (layer.flags & ~s.mask) | (s.flags & s.mask);
        }
        if (s.what & layer_state_t::ePositionChanged) {
            layer.x = s.x;
            layer.y = s.y;
        }
        if (s.what & layer_state_t::eLayerChanged) {
            layer.z = s.z;
        }
        if (s.what & layer_state_t::eDestinationFrameChanged) {
            layer.destinationFrame = s.destinationFrame;
        }
        if (s.what & layer_state_t::eCropChanged) {
            layer.crop = s.crop;
        }
        if (s.what & layer_state_t::eAlphaChanged) {
            layer.alpha = static_cast<float>(s.color.a);
        }
        if (s.what & layer_state_t::eColorChanged) {
            layer.color = (layer.color & 0xFF000000u) |
                    (halfChannel(s.color.r) << 16) |
                    (halfChannel(s.color.g) << 8) |
                    halfChannel(s.color.b);
            layer.hasColor = true;
        }
        if (s.what & layer_state_t::eMatrixChanged) {
            layer.matrix[0] = s.matrix.dsdx;
            layer.matrix[1] = s.matrix.dtdx;
            layer.matrix[2] = s.matrix.dtdy;
            layer.matrix[3] = s.matrix.dsdy;
        }
        if (s.what & layer_state_t::eBufferCropChanged) {
            layer.bufferCrop = s.bufferCrop;
            layer.hasBufferCrop = !s.bufferCrop.isEmpty();
        }
        if (s.what & layer_state_t::eCornerRadiusChanged) {
            layer.cornerRadii = s.cornerRadii;
            layer.hasCornerRadii = !s.cornerRadii.isEmpty();
        }
        if ((s.what & layer_state_t::eBufferChanged) && s.bufferData) {
            latchBuffer(layer, *s.bufferData);
        }
    }

    static uint8_t halfChannel(android::half value) {
        const float scaled = static_cast<float>(value) * 255.0f;
        return static_cast<uint8_t>(std::clamp(std::lround(scaled), 0L, 255L));
    }

    static uint32_t sourceOver(uint32_t src, uint32_t dst, float alpha,
                               bool preserveAlpha = false) {
        uint32_t srcAlpha = (src >> 24) & 0xFFu;
        srcAlpha = static_cast<uint32_t>(
                std::clamp(std::lround(srcAlpha * std::clamp(alpha, 0.0f, 1.0f)), 0L, 255L));
        if (srcAlpha == 0) {
            return dst;
        }
        if (srcAlpha == 255) {
            return src | 0xFF000000u;
        }
        if (preserveAlpha) {
            const uint32_t dstAlpha = (dst >> 24) & 0xFFu;
            const uint32_t dstScale = dstAlpha * (255u - srcAlpha) / 255u;
            const uint32_t outAlpha = srcAlpha + dstScale;
            const uint32_t denom = outAlpha == 0 ? 1 : outAlpha;
            const uint32_t r = ((((src >> 16) & 0xFFu) * srcAlpha) +
                                (((dst >> 16) & 0xFFu) * dstScale) + denom / 2) / denom;
            const uint32_t g = ((((src >> 8) & 0xFFu) * srcAlpha) +
                                (((dst >> 8) & 0xFFu) * dstScale) + denom / 2) / denom;
            const uint32_t b = ((((src & 0xFFu) * srcAlpha) +
                                ((dst & 0xFFu) * dstScale) + denom / 2) / denom);
            return (outAlpha << 24) | (r << 16) | (g << 8) | b;
        }
        const uint32_t inv = 255u - srcAlpha;
        const uint32_t r = ((((src >> 16) & 0xFFu) * srcAlpha) +
                            (((dst >> 16) & 0xFFu) * inv) + 127u) / 255u;
        const uint32_t g = ((((src >> 8) & 0xFFu) * srcAlpha) +
                            (((dst >> 8) & 0xFFu) * inv) + 127u) / 255u;
        const uint32_t b = ((((src & 0xFFu) * srcAlpha) +
                            ((dst & 0xFFu) * inv) + 127u)) / 255u;
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    void latchBuffer(Layer& layer, const BufferData& data) {
        sp<GraphicBuffer> buffer = data.buffer;
        if (buffer != nullptr && data.cachedBuffer.id != 0) {
            mBufferCache[data.cachedBuffer.id] = buffer;
        } else if (buffer == nullptr && data.cachedBuffer.id != 0) {
            auto it = mBufferCache.find(data.cachedBuffer.id);
            if (it != mBufferCache.end()) {
                buffer = it->second;
            } else {
                ALOGW("layer %d references unknown cached buffer %" PRIu64, layer.id,
                      data.cachedBuffer.id);
            }
        }

        // The buffer that is still latched was never composed (composition
        // releases it immediately), so the producer can have it back.  Count
        // it as dropped: the newer buffer superseded it before presentation.
        if (layer.hasBuffer && layer.releaseListener != nullptr) {
            ++mDroppedBuffers;
        }
        releaseLayerBuffer(layer);

        if (buffer == nullptr) {
            layer.hasBuffer = false;
            return;
        }

        ++mLatchedBuffers;
        ALOGV("layer %d latch %ux%u stride %u format 0x%x frame %" PRIu64, layer.id,
              buffer->getWidth(), buffer->getHeight(), buffer->getStride(),
              buffer->getPixelFormat(), data.frameNumber);
        layer.buffer = buffer;
        layer.frameNumber = data.frameNumber;
        layer.acquireFence = data.acquireFence;
        layer.releaseListener = data.releaseBufferListener;
        layer.releaseId = data.generateReleaseCallbackId();
        layer.hasBuffer = true;
    }

    void releaseLayerBuffer(Layer& layer) {
        sp<ITransactionCompletedListener> listener = std::move(layer.releaseListener);
        ReleaseCallbackId id = layer.releaseId;
        layer.releaseListener = nullptr;
        layer.releaseId = ReleaseCallbackId{};
        if (listener != nullptr) {
            listener->onReleaseBuffer(id, Fence::NO_FENCE, kMaxAcquiredBuffers,
                                      false /* removeFromCache */);
        }
    }

    std::vector<ListenerStats> collectCallbacksLocked(const TransactionState& state) {
        std::vector<ListenerStats> out;

        struct LayerStats {
            sp<IBinder> handle;
            SurfaceStats stats;
        };
        std::vector<LayerStats> layerStats;

        for (const auto& composerState : state.mComposerStates) {
            const layer_state_t& s = composerState.state;
            auto it = mByHandle.find(s.surface.get());
            if (it == mByHandle.end() || !it->second->hasBuffer) {
                continue;
            }
            LayerStats entry;
            entry.handle = s.surface;
            entry.stats = makeSurfaceStats(*it->second);
            layerStats.push_back(std::move(entry));
        }

        nsecs_t latchTime = systemTime(SYSTEM_TIME_MONOTONIC);

        auto addListener = [&](const sp<IBinder>& listener,
                               const std::vector<CallbackId>& ids,
                               const sp<IBinder>& onlySurface) {
            if (listener == nullptr || ids.empty()) {
                return;
            }
            ListenerStats listenerStats;
            listenerStats.listener = listener;
            TransactionStats transactionStats(ids, latchTime, Fence::NO_FENCE, {});
            for (const auto& entry : layerStats) {
                if (onlySurface != nullptr && entry.handle != onlySurface) {
                    continue;
                }
                transactionStats.surfaceStats.push_back(entry.stats);
            }
            listenerStats.transactionStats.push_back(std::move(transactionStats));
            out.push_back(std::move(listenerStats));
        };

        // Layer-attached callbacks (BLASTBufferQueue registers one per layer).
        for (const auto& composerState : state.mComposerStates) {
            const layer_state_t& s = composerState.state;
            for (const auto& listener : s.listeners) {
                addListener(listener.transactionCompletedListener, listener.callbackIds,
                            s.surface);
            }
        }

        // Callbacks that have no SurfaceControl attached are flattened onto the
        // transaction itself.
        for (const auto& listener : state.mCallbacks.mFlattenedListenerCallbacks) {
            addListener(listener.transactionCompletedListener, listener.callbackIds, nullptr);
        }

        return out;
    }

    SurfaceStats makeSurfaceStats(const Layer& layer) {
        FrameEventHistoryStats eventStats;
        eventStats.frameNumber = layer.frameNumber;
        eventStats.previousFrameNumber = 0;
        eventStats.refreshStartTime = systemTime(SYSTEM_TIME_MONOTONIC);
        eventStats.dequeueReadyTime = 0;

        return SurfaceStats(layer.handle, systemTime(SYSTEM_TIME_MONOTONIC), Fence::NO_FENCE,
                            std::nullopt, kMaxAcquiredBuffers, std::nullopt, eventStats,
                            ReleaseCallbackId{});
    }

    void fireCallbacks(const std::vector<std::unique_ptr<TransactionState>>& /*presented*/) {
        // The transaction objects themselves only needed to stay alive until the
        // frame was presented (they own the buffer references).  The callback
        // data was snapshotted when the transaction was applied.
        std::vector<PendingFrame> pending;
        pending.swap(mPendingCallbacks);
        if (pending.empty()) {
            return;
        }
        ensureWorkerAttached();

        for (const auto& frame : pending) {
            for (const auto& listenerStats : frame.listenerStats) {
                sp<ITransactionCompletedListener> listener =
                        interface_cast<ITransactionCompletedListener>(listenerStats.listener);
                if (listener == nullptr) {
                    ALOGW("composer listener is not an ITransactionCompletedListener");
                    continue;
                }
                listener->onTransactionCompleted(listenerStats);
            }
        }
    }

    // -- composition --------------------------------------------------------

    void dumpLayers() {
        std::lock_guard lock(mLayerMutex);
        ALOGI("layers: %zu", mLayers.size());
        for (const auto& entry : mLayers) {
            const Layer& layer = *entry.second;
            ALOGI("layer id=%d name='%s' pos=%.1f,%.1f z=%d flags=0x%x buffer=%d",
                  layer.id, layer.name.c_str(), layer.x, layer.y, layer.z, layer.flags,
                  layer.hasBuffer ? 1 : 0);
        }
    }

    // DROIDLET_COMPOSER_DUMP=/path.ppm writes the most recent composed frame.
    // It is a diagnostic aid for headless runs ("null" presenter) and is
    // refreshed every 30 presented frames.
    void dumpFrame() {
        const char* path = std::getenv("DROIDLET_COMPOSER_DUMP");
        if (path == nullptr || *path == '\0') {
            return;
        }
        FILE* file = std::fopen(path, "wb");
        if (file == nullptr) {
            return;
        }
        std::fprintf(file, "P6\n%d %d\n255\n", mWidth, mHeight);
        std::vector<uint8_t> row(static_cast<size_t>(mWidth) * 3);
        for (int y = 0; y < mHeight; ++y) {
            const uint32_t* src = mFrame.data() + static_cast<int64_t>(y) * mWidth;
            for (int x = 0; x < mWidth; ++x) {
                row[x * 3 + 0] = static_cast<uint8_t>((src[x] >> 16) & 0xFF);
                row[x * 3 + 1] = static_cast<uint8_t>((src[x] >> 8) & 0xFF);
                row[x * 3 + 2] = static_cast<uint8_t>(src[x] & 0xFF);
            }
            std::fwrite(row.data(), 1, row.size(), file);
        }
        std::fclose(file);
    }

    int32_t layerOriginX(const Layer& layer, int depth = 0) const {
        if (depth > 16 || layer.parent == nullptr) {
            return static_cast<int32_t>(std::lround(layer.x));
        }
        auto it = mByHandle.find(layer.parent.get());
        if (it == mByHandle.end()) {
            return static_cast<int32_t>(std::lround(layer.x));
        }
        return layerOriginX(*it->second, depth + 1) + static_cast<int32_t>(std::lround(layer.x));
    }

    int32_t layerOriginY(const Layer& layer, int depth = 0) const {
        if (depth > 16 || layer.parent == nullptr) {
            return static_cast<int32_t>(std::lround(layer.y));
        }
        auto it = mByHandle.find(layer.parent.get());
        if (it == mByHandle.end()) {
            return static_cast<int32_t>(std::lround(layer.y));
        }
        return layerOriginY(*it->second, depth + 1) + static_cast<int32_t>(std::lround(layer.y));
    }

    // Absolute bounds of a layer in display coordinates, if they are known.
    // Container layers have no intrinsic size; their children are clipped by
    // whichever ancestor does have bounds (usually the window layer).
    Rect layerBoundsLocked(const Layer& layer, int depth = 0) const {
        if (layer.hasBuffer && layer.buffer != nullptr) {
            Rect dest = layer.destinationFrame;
            if (dest.isEmpty()) {
                dest = Rect(static_cast<int32_t>(layer.buffer->getWidth()),
                            static_cast<int32_t>(layer.buffer->getHeight()));
            }
            const int32_t x = layerOriginX(layer) + dest.left;
            const int32_t y = layerOriginY(layer) + dest.top;
            return Rect(x, y, x + dest.getWidth(), y + dest.getHeight());
        }
        if (layer.hasColor && !layer.destinationFrame.isEmpty()) {
            const int32_t x = layerOriginX(layer) + layer.destinationFrame.left;
            const int32_t y = layerOriginY(layer) + layer.destinationFrame.top;
            return Rect(x, y, x + layer.destinationFrame.getWidth(),
                        y + layer.destinationFrame.getHeight());
        }
        // Container layers have no buffer of their own.  SurfaceView uses
        // SurfaceControl.setWindowCrop() on the container to clip its child
        // surface, so treat a valid crop as the container bounds.
        if (layer.crop.getWidth() > 0.0f && layer.crop.getHeight() > 0.0f) {
            const int32_t x = layerOriginX(layer) +
                    static_cast<int32_t>(std::lround(layer.crop.left));
            const int32_t y = layerOriginY(layer) +
                    static_cast<int32_t>(std::lround(layer.crop.top));
            return Rect(x, y,
                        x + static_cast<int32_t>(std::lround(layer.crop.getWidth())),
                        y + static_cast<int32_t>(std::lround(layer.crop.getHeight())));
        }
        return Rect();
    }

    Rect parentClipLocked(const Layer& layer) const {
        Rect clip(0, 0, mWidth, mHeight);
        const Layer* current = &layer;
        for (int depth = 0; depth < 16 && current->parent != nullptr; ++depth) {
            auto it = mByHandle.find(current->parent.get());
            if (it == mByHandle.end()) {
                break;
            }
            current = it->second.get();
            Rect bounds = layerBoundsLocked(*current, depth);
            if (!bounds.isEmpty()) {
                Rect intersection;
                if (clip.intersect(bounds, &intersection)) {
                    clip = intersection;
                } else {
                    clip = Rect();
                }
            }
        }
        return clip;
    }

    bool layerVisibleLocked(const Layer& layer, int depth = 0) const {
        if (!layer.isContainer && (layer.flags & layer_state_t::eLayerHidden) != 0) {
            return false;
        }
        if (depth > 16 || layer.parent == nullptr) {
            return true;
        }
        auto it = mByHandle.find(layer.parent.get());
        if (it == mByHandle.end()) {
            return true;
        }
        return layerVisibleLocked(*it->second, depth + 1);
    }

    static bool isSupportedFormat(int32_t format) {
        switch (format) {
            case PIXEL_FORMAT_RGBA_8888:
            case PIXEL_FORMAT_RGBX_8888:
            case PIXEL_FORMAT_BGRA_8888:
            case PIXEL_FORMAT_RGB_888:
            case PIXEL_FORMAT_RGB_565:
                return true;
            default:
                return false;
        }
    }

    // Copies one 32-bit source pixel.  All 32-bit host buffers are written by
    // hwui through kN32, i.e. their words are 0xAARRGGBB.
    static uint32_t readPixel(const uint8_t* row, int x, int32_t format) {
        switch (format) {
            case PIXEL_FORMAT_RGB_888: {
                const uint8_t* p = row + x * 3;
                return 0xFF000000u | (static_cast<uint32_t>(p[2]) << 16) |
                        (static_cast<uint32_t>(p[1]) << 8) | p[0];
            }
            case PIXEL_FORMAT_RGB_565: {
                const uint16_t* p = reinterpret_cast<const uint16_t*>(row) + x;
                uint16_t v = *p;
                uint32_t r = (v >> 11) & 0x1F;
                uint32_t g = (v >> 5) & 0x3F;
                uint32_t b = v & 0x1F;
                return 0xFF000000u | ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) |
                        (b * 255 / 31);
            }
            case PIXEL_FORMAT_RGBA_8888:
            case PIXEL_FORMAT_RGBX_8888: {
                // Byte order is R,G,B,A/X.  On little-endian hosts the word is
                // 0xAABBGGRR (or 0xXXBBGGRR); return 0xAARRGGBB.
                const uint32_t value = reinterpret_cast<const uint32_t*>(row)[x];
                const uint32_t alpha = (format == PIXEL_FORMAT_RGBX_8888)
                        ? 0xFFu : (value >> 24) & 0xFFu;
                return (alpha << 24) | ((value & 0xFFu) << 16) |
                        (((value >> 8) & 0xFFu) << 8) | ((value >> 16) & 0xFFu);
            }
            case PIXEL_FORMAT_BGRA_8888:
                return reinterpret_cast<const uint32_t*>(row)[x];
            default:
                return reinterpret_cast<const uint32_t*>(row)[x];
        }
    }

    void composeFrame() {
        std::fill(mFrame.begin(), mFrame.end(), 0xFF000000u);

        std::vector<std::shared_ptr<Layer>> layers;
        {
            std::lock_guard lock(mLayerMutex);
            layers.reserve(mLayers.size());
            for (const auto& [id, layer] : mLayers) {
                if ((layer->hasBuffer || layer->hasColor) && layerVisibleLocked(*layer)) {
                    layers.push_back(layer);
                }
            }
            std::stable_sort(layers.begin(), layers.end(),
                             [](const std::shared_ptr<Layer>& a, const std::shared_ptr<Layer>& b) {
                                 if (a->z != b->z) {
                                     return a->z < b->z;
                                 }
                                 // A color/effect layer with the same z as a
                                 // buffer layer is usually a background (the
                                 // SurfaceView background is the common case).
                                 const bool aColor = a->hasColor && !a->hasBuffer;
                                 const bool bColor = b->hasColor && !b->hasBuffer;
                                 if (aColor != bColor) {
                                     return aColor;
                                 }
                                 return a->id < b->id;
                             });
        }

        bool sharedMode = false;
        int minSharedZ = std::numeric_limits<int>::max();
        {
            std::lock_guard lock(mLayerMutex);
            for (const auto& [id, layer] : mLayers) {
                if (layer->shared) {
                    sharedMode = true;
                    minSharedZ = std::min(minSharedZ, layer->z);
                }
            }
        }

        auto mixHash = [](uint64_t h, uint64_t v) {
            h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            return h;
        };
        uint64_t parentHash = 0x6a09e667f3bcc909ull;
        Rect effectBounds;

        mLastLayerCount = static_cast<int>(layers.size());
        if (sharedMode && mPresenterEffect) {
            std::fill(mEffectFrame.begin(), mEffectFrame.end(), 0);
            bool effectActive = false;
            int effectZ = 0;
            for (const auto& layer : layers) {
                if (layer->shared) {
                    continue;
                }
                // Color layers at or below the lowest shared surface (the
                // SurfaceView background) stay in the parent frame.  A buffer
                // layer at that z is the SurfaceView content itself: it must be
                // drawn into the effect overlay, above the shared app surface,
                // because the shared app subsurface is opaque and hides the
                // parent frame (otherwise video plays to a black rectangle).
                const bool inEffect = layer->z > minSharedZ
                        || (layer->hasBuffer && layer->z >= minSharedZ);
                mDrawTarget = inEffect ? &mEffectFrame : &mFrame;
                if (inEffect) {
                    effectActive = true;
                    effectZ = std::max(effectZ, layer->z);
                    Rect bounds = layerBoundsLocked(*layer);
                    if (!bounds.isEmpty()) {
                        bounds.left = std::clamp(bounds.left, 0, mWidth);
                        bounds.right = std::clamp(bounds.right, 0, mWidth);
                        bounds.top = std::clamp(bounds.top, 0, mHeight);
                        bounds.bottom = std::clamp(bounds.bottom, 0, mHeight);
                        if (!bounds.isEmpty()) {
                            if (effectBounds.isEmpty()) {
                                effectBounds = bounds;
                            } else {
                                effectBounds.left = std::min(effectBounds.left, bounds.left);
                                effectBounds.top = std::min(effectBounds.top, bounds.top);
                                effectBounds.right = std::max(effectBounds.right, bounds.right);
                                effectBounds.bottom = std::max(effectBounds.bottom, bounds.bottom);
                            }
                        }
                    }
                } else {
                    parentHash = mixHash(parentHash, static_cast<uint64_t>(layer->id));
                    parentHash = mixHash(parentHash,
                                         reinterpret_cast<uintptr_t>(layer->buffer.get()));
                    parentHash = mixHash(parentHash, layer->frameNumber);
                    parentHash = mixHash(parentHash, layer->color);
                    parentHash = mixHash(parentHash, layer->hasColor ? 1 : 0);
                    const Rect bounds = layerBoundsLocked(*layer);
                    parentHash = mixHash(parentHash, static_cast<uint64_t>(bounds.left));
                    parentHash = mixHash(parentHash, static_cast<uint64_t>(bounds.top));
                    parentHash = mixHash(parentHash, static_cast<uint64_t>(bounds.right));
                    parentHash = mixHash(parentHash, static_cast<uint64_t>(bounds.bottom));
                    parentHash = mixHash(parentHash,
                                         static_cast<uint64_t>(layerOriginX(*layer)));
                    parentHash = mixHash(parentHash,
                                         static_cast<uint64_t>(layerOriginY(*layer)));
                }
                if (layer->hasBuffer && layer->buffer != nullptr) {
                    drawLayer(*layer);
                } else if (layer->hasColor) {
                    drawColorLayer(*layer);
                }
            }
            mEffectActive = effectActive;
            mEffectZ = effectZ;
            mSharedMode = true;
            mEffectBounds = effectBounds;
            mParentHash = parentHash;
            mDrawTarget = &mFrame;
        } else {
            mDrawTarget = &mFrame;
            for (const auto& layer : layers) {
                if (layer->hasBuffer && layer->buffer != nullptr) {
                    drawLayer(*layer);
                } else if (layer->hasColor) {
                    drawColorLayer(*layer);
                }
            }
            if (mEffectWasActive) {
                // The presenter keeps the last effect buffer attached; clear
                // it once so a dismissed dialog does not leave a stale scrim.
                std::fill(mEffectFrame.begin(), mEffectFrame.end(), 0);
            }
            mEffectActive = false;
            mSharedMode = false;
            mEffectBounds = Rect();
            mParentHash = 0;
        }
        if (std::getenv("DROIDLET_COMPOSER_DEBUG") != nullptr) {
            static auto lastLog = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (now - lastLog >= std::chrono::seconds(1)) {
                lastLog = now;
                ALOGI("compose: shared=%d effectActive=%d effectZ=%d layers=%zu",
                      sharedMode ? 1 : 0, mEffectActive ? 1 : 0, mEffectZ, layers.size());
                for (const auto& layer : layers) {
                    ALOGI("  layer id=%d z=%d shared=%d color=%d buffer=%d alpha=%.2f",
                          layer->id, layer->z, layer->shared ? 1 : 0,
                          layer->hasColor ? 1 : 0, layer->hasBuffer ? 1 : 0, layer->alpha);
                }
            }
        }
        if (mPresenterOverlay) {
            std::fill(mOverlayFrame.begin(), mOverlayFrame.end(), 0);
            mPerfOverlay.drawOverlay(mOverlayFrame, kOverlayWidth, kOverlayHeight);
        } else {
            mPerfOverlay.draw(*mDrawTarget, mWidth, mHeight);
        }

        // Buffers are only kept alive until the pixels have been copied, so the
        // producer never needs more than a couple of them in flight.
        std::vector<std::shared_ptr<Layer>> consumed;
        {
            std::lock_guard lock(mLayerMutex);
            for (const auto& layer : layers) {
                if (layer->releaseListener != nullptr) {
                    consumed.push_back(layer);
                }
            }
            for (const auto& layer : consumed) {
                releaseLayerBuffer(*layer);
            }
        }
    }

    void drawLayer(const Layer& layer) {
        const int32_t format = layer.buffer->getPixelFormat();
        if (!isSupportedFormat(format)) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                ALOGW("unsupported buffer format %d; layer %d will not be drawn", format,
                      layer.id);
            }
            return;
        }

        if (layer.acquireFence != nullptr && layer.acquireFence->isValid()) {
            layer.acquireFence->wait(1000);
        }

        void* bits = nullptr;
        status_t err = layer.buffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, &bits);
        if (err != NO_ERROR || bits == nullptr) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                ALOGW("unable to map layer %d buffer: %s", layer.id, strerror(-err));
            }
            return;
        }

        const int32_t bufferWidth = static_cast<int32_t>(layer.buffer->getWidth());
        const int32_t bufferHeight = static_cast<int32_t>(layer.buffer->getHeight());
        const int32_t stride = static_cast<int32_t>(layer.buffer->getStride());
        const int32_t bytesPerPixel = (format == PIXEL_FORMAT_RGB_888) ? 3
                : (format == PIXEL_FORMAT_RGB_565)                     ? 2
                                                                       : 4;
        const auto* src = static_cast<const uint8_t*>(bits);
        const int32_t rowBytes = stride * bytesPerPixel;

        FloatRect crop = layer.crop;
        if (layer.hasBufferCrop) {
            crop = FloatRect(static_cast<float>(layer.bufferCrop.left),
                             static_cast<float>(layer.bufferCrop.top),
                             static_cast<float>(layer.bufferCrop.right),
                             static_cast<float>(layer.bufferCrop.bottom));
        }
        if (!(crop.getWidth() > 0.0f) || !(crop.getHeight() > 0.0f)) {
            crop = FloatRect(0.0f, 0.0f, static_cast<float>(bufferWidth),
                             static_cast<float>(bufferHeight));
        }
        crop = crop.intersect(FloatRect(0.0f, 0.0f, static_cast<float>(bufferWidth),
                                        static_cast<float>(bufferHeight)));

        Rect dest = layer.destinationFrame;
        if (dest.isEmpty()) {
            dest = Rect(bufferWidth, bufferHeight);
        }

        const int32_t destX = layerOriginX(layer) + dest.left;
        const int32_t destY = layerOriginY(layer) + dest.top;
        const int32_t destWidth = dest.getWidth();
        const int32_t destHeight = dest.getHeight();
        if (destWidth <= 0 || destHeight <= 0) {
            layer.buffer->unlock();
            return;
        }

        const Rect parentClip = parentClipLocked(layer);
        const int32_t clipLeft = std::max(parentClip.left, destX);
        const int32_t clipTop = std::max(parentClip.top, destY);
        const int32_t clipRight = std::min(parentClip.right, destX + destWidth);
        const int32_t clipBottom = std::min(parentClip.bottom, destY + destHeight);

        const double scaleX = static_cast<double>(crop.getWidth()) / destWidth;
        const double scaleY = static_cast<double>(crop.getHeight()) / destHeight;

        // layer_state_t.matrix maps layer space to buffer space.  Invert it so
        // each destination pixel can sample the untransformed buffer.  Most
        // layers use the identity matrix.
        const float m00 = layer.matrix[0];
        const float m01 = layer.matrix[1];
        const float m10 = layer.matrix[2];
        const float m11 = layer.matrix[3];
        const float det = m00 * m11 - m01 * m10;
        const bool identity = std::abs(m00 - 1.0f) < 1e-6f && std::abs(m01) < 1e-6f &&
                std::abs(m10) < 1e-6f && std::abs(m11 - 1.0f) < 1e-6f;
        const bool invertible = identity || std::abs(det) > 1e-6f;

        // Fast path for the common single full-screen opaque layer.  This is
        // the app window in games and most full-screen activities; avoiding the
        // per-pixel source-over sampling cuts the composer time substantially.
        const bool fullFrame = identity && !layer.hasCornerRadii && layer.alpha >= 1.0f &&
                destX == 0 && destY == 0 && destWidth == mWidth && destHeight == mHeight &&
                bufferWidth == mWidth && bufferHeight == mHeight &&
                crop.left <= 0.0f && crop.top <= 0.0f &&
                crop.right >= static_cast<float>(bufferWidth) &&
                crop.bottom >= static_cast<float>(bufferHeight) &&
                clipLeft == 0 && clipTop == 0 && clipRight == mWidth && clipBottom == mHeight;
        if (fullFrame) {
            for (int32_t y = 0; y < mHeight; ++y) {
                const uint8_t* srcRow = src + static_cast<int64_t>(y) * rowBytes;
                uint32_t* dstRow = mDrawTarget->data() + static_cast<int64_t>(y) * mWidth;
                switch (format) {
                    case PIXEL_FORMAT_BGRA_8888:
                        std::memcpy(dstRow, srcRow, static_cast<size_t>(mWidth) * 4);
                        break;
                    case PIXEL_FORMAT_RGBA_8888:
                    case PIXEL_FORMAT_RGBX_8888:
                        for (int32_t x = 0; x < mWidth; ++x) {
                            const uint32_t v = reinterpret_cast<const uint32_t*>(srcRow)[x];
                            const uint32_t a = (format == PIXEL_FORMAT_RGBX_8888)
                                    ? 0xFFu : ((v >> 24) & 0xFFu);
                            dstRow[x] = (a << 24) | ((v & 0xFFu) << 16) |
                                    (((v >> 8) & 0xFFu) << 8) | ((v >> 16) & 0xFFu);
                        }
                        break;
                    case PIXEL_FORMAT_RGB_888:
                        for (int32_t x = 0; x < mWidth; ++x) {
                            const uint8_t* p = srcRow + x * 3;
                            dstRow[x] = 0xFF000000u | (static_cast<uint32_t>(p[2]) << 16) |
                                    (static_cast<uint32_t>(p[1]) << 8) | p[0];
                        }
                        break;
                    case PIXEL_FORMAT_RGB_565:
                        for (int32_t x = 0; x < mWidth; ++x) {
                            const uint16_t p = reinterpret_cast<const uint16_t*>(srcRow)[x];
                            const uint32_t r = (p >> 11) & 0x1F;
                            const uint32_t g = (p >> 5) & 0x3F;
                            const uint32_t b = p & 0x1F;
                            dstRow[x] = 0xFF000000u | ((r * 255 / 31) << 16) |
                                    ((g * 255 / 63) << 8) | (b * 255 / 31);
                        }
                        break;
                    default:
                        break;
                }
            }
            layer.buffer->unlock();
            return;
        }

        for (int32_t y = clipTop; y < clipBottom; ++y) {
            const float localY = (y - destY) * scaleY;
            const float ly = static_cast<float>(y - destY);
            uint32_t* dstRow = mDrawTarget->data() + static_cast<int64_t>(y) * mWidth;
            for (int32_t x = clipLeft; x < clipRight; ++x) {
                const float lx = static_cast<float>(x - destX);
                if (layer.hasCornerRadii &&
                    !insideRoundedRect(lx, ly, static_cast<float>(destWidth),
                                       static_cast<float>(destHeight),
                                       layer.cornerRadii)) {
                    continue;
                }

                float sx = static_cast<float>(x - destX) * scaleX;
                float sy = localY;
                if (!identity) {
                    if (!invertible) {
                        continue;
                    }
                    const float bx = (m11 * sx - m01 * sy) / det;
                    const float by = (-m10 * sx + m00 * sy) / det;
                    sx = bx;
                    sy = by;
                }
                sx += crop.left;
                sy += crop.top;
                if (sx < crop.left || sx >= crop.right || sy < crop.top || sy >= crop.bottom) {
                    continue;
                }
                const int32_t srcX = std::clamp(static_cast<int32_t>(sx), 0, bufferWidth - 1);
                const int32_t srcY = std::clamp(static_cast<int32_t>(sy), 0, bufferHeight - 1);
                const uint8_t* srcRow = src + static_cast<int64_t>(srcY) * rowBytes;
                dstRow[x] = sourceOver(readPixel(srcRow, srcX, format), dstRow[x], layer.alpha,
                                       mDrawTarget == &mEffectFrame);
            }
        }

        layer.buffer->unlock();
    }

    static bool insideRoundedRect(float x, float y, float width, float height,
            const gui::CornerRadii& radii) {
        if (x < 0.0f || y < 0.0f || x >= width || y >= height) {
            return false;
        }
        auto inside = [](float px, float py, float rx, float ry) {
            if (rx <= 0.0f || ry <= 0.0f) {
                return true;
            }
            const float nx = px / rx;
            const float ny = py / ry;
            return nx * nx + ny * ny <= 1.0f;
        };
        if (x < radii.topLeft.x && y < radii.topLeft.y) {
            return inside(radii.topLeft.x - x, radii.topLeft.y - y,
                          radii.topLeft.x, radii.topLeft.y);
        }
        if (x >= width - radii.topRight.x && y < radii.topRight.y) {
            return inside(x - (width - radii.topRight.x), radii.topRight.y - y,
                          radii.topRight.x, radii.topRight.y);
        }
        if (x < radii.bottomLeft.x && y >= height - radii.bottomLeft.y) {
            return inside(radii.bottomLeft.x - x, y - (height - radii.bottomLeft.y),
                          radii.bottomLeft.x, radii.bottomLeft.y);
        }
        if (x >= width - radii.bottomRight.x && y >= height - radii.bottomRight.y) {
            return inside(x - (width - radii.bottomRight.x),
                          y - (height - radii.bottomRight.y),
                          radii.bottomRight.x, radii.bottomRight.y);
        }
        return true;
    }

    void drawColorLayer(const Layer& layer) {
        Rect dest = layer.destinationFrame;
        if (dest.isEmpty()) {
            dest = Rect(mWidth, mHeight);
        }
        const int32_t destX = layerOriginX(layer) + dest.left;
        const int32_t destY = layerOriginY(layer) + dest.top;
        const int32_t destWidth = dest.getWidth();
        const int32_t destHeight = dest.getHeight();
        if (destWidth <= 0 || destHeight <= 0) {
            return;
        }
        const Rect parentClip = parentClipLocked(layer);
        const int32_t clipLeft = std::max(parentClip.left, destX);
        const int32_t clipTop = std::max(parentClip.top, destY);
        const int32_t clipRight = std::min(parentClip.right, destX + destWidth);
        const int32_t clipBottom = std::min(parentClip.bottom, destY + destHeight);
        for (int32_t y = clipTop; y < clipBottom; ++y) {
            const float ly = static_cast<float>(y - destY);
            uint32_t* dstRow = mDrawTarget->data() + static_cast<int64_t>(y) * mWidth;
            for (int32_t x = clipLeft; x < clipRight; ++x) {
                const float lx = static_cast<float>(x - destX);
                if (layer.hasCornerRadii &&
                    !insideRoundedRect(lx, ly, static_cast<float>(destWidth),
                                       static_cast<float>(destHeight),
                                       layer.cornerRadii)) {
                    continue;
                }
                dstRow[x] = sourceOver(layer.color, dstRow[x], layer.alpha,
                                       mDrawTarget == &mEffectFrame);
            }
        }
    }

    // -- state --------------------------------------------------------------

    int mWidth;
    int mHeight;

    static constexpr int kOverlayWidth = 720;
    static constexpr int kOverlayHeight = 240;

    std::unique_ptr<Presenter> mPresenter;
    std::vector<uint32_t> mFrame;
    std::vector<uint32_t> mEffectFrame;
    std::vector<uint32_t> mOverlayFrame;
    std::vector<uint32_t>* mDrawTarget = nullptr;
    bool mPresenterOverlay = false;
    bool mPresenterEffect = false;
    bool mEffectActive = false;
    bool mEffectWasActive = false;
    bool mSharedMode = false;
    int mEffectZ = 0;
    mutable std::mutex mFrameMutex;
    std::condition_variable mFrameCv;
    uint64_t mFrameSequence = 0;

    std::thread mWorker;
    std::mutex mQueueMutex;
    std::condition_variable mQueueCv;
    std::condition_variable mTransactionsAppliedCv;
    std::atomic<size_t> mPendingTransactions{0};
    bool mStop = false;
    std::vector<std::unique_ptr<TransactionState>> mQueue;
    std::vector<std::unique_ptr<TransactionState>> mPending;
    std::vector<PendingFrame> mPendingCallbacks;
    std::atomic<uint64_t> mPresentedFrames{0};
    std::atomic<uint64_t> mEffectPresented{0};
    std::atomic<uint64_t> mEffectDropped{0};
    Rect mEffectBounds;
    Rect mLastEffectBounds;
    uint64_t mParentHash = 0;
    uint64_t mLastParentHash = ~0ull;
    std::atomic<uint64_t> mLatchedBuffers{0};
    std::atomic<bool> mRegistryDirty{false};
    bool mFpsEnabled = false;
    bool mTraceFrames = false;
    bool mDumpLayers = false;
    uint64_t mFpsFrames = 0;
    std::chrono::steady_clock::time_point mFpsStart;
    std::atomic<uint64_t> mDroppedBuffers{0};
    uint64_t mLastDumpLatch = 0;
    int mLastLayerCount = 0;
    PerfOverlay mPerfOverlay;

    mutable std::mutex mLayerMutex;
    int32_t mNextLayerId = 1;
    std::map<int32_t, std::shared_ptr<Layer>> mLayers;
    std::unordered_map<const IBinder*, std::shared_ptr<Layer>> mByHandle;
    std::unordered_map<uint64_t, sp<GraphicBuffer>> mBufferCache;
};

HostComposer* gComposer = nullptr;
sp<IBinder> gServices[2];

// ---------------------------------------------------------------------------
// gui::ISurfaceComposerClient: layer creation.
// ---------------------------------------------------------------------------

class HostComposerClient final : public gui::ISurfaceComposerClientDefault {
public:
    explicit HostComposerClient(HostComposer* composer) : mComposer(composer) {}

    binder::Status createSurface(const std::string& name, int32_t flags,
                                 const sp<IBinder>& parent,
                                 const gui::LayerMetadata& /*metadata*/,
                                 gui::CreateSurfaceResult* result) override {
        if (result == nullptr) {
            return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_ARGUMENT);
        }
        int32_t id = -1;
        sp<IBinder> handle = mComposer->addLayer(name, flags, parent, &id);
        if (handle == nullptr || id < 0) {
            return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_STATE);
        }
        result->handle = handle;
        result->layerId = id;
        result->layerName = String16(name.c_str(), name.size());
        result->transformHint = 0;
        return binder::Status::ok();
    }

    binder::Status clearLayerFrameStats(const sp<IBinder>& /*handle*/) override {
        return binder::Status::ok();
    }

    binder::Status getLayerFrameStats(const sp<IBinder>& /*handle*/,
                                      gui::FrameStats* outStats) override {
        if (outStats == nullptr) {
            return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_ARGUMENT);
        }
        *outStats = gui::FrameStats();
        return binder::Status::ok();
    }

    binder::Status mirrorSurface(const sp<IBinder>& /*mirrorFromHandle*/,
                                 const sp<IBinder>& /*stopAtHandle*/,
                                 const sp<IBinder>& /*cropByHandle*/,
                                 gui::CreateSurfaceResult* /*result*/) override {
        return binder::Status::fromExceptionCode(binder::Status::EX_UNSUPPORTED_OPERATION);
    }

    binder::Status mirrorLayerStack(int64_t /*displayId*/,
                                    gui::CreateSurfaceResult* /*result*/) override {
        return binder::Status::fromExceptionCode(binder::Status::EX_UNSUPPORTED_OPERATION);
    }

    binder::Status mirrorDisplay(int64_t /*displayId*/,
                                 gui::CreateSurfaceResult* /*result*/) override {
        return binder::Status::fromExceptionCode(binder::Status::EX_UNSUPPORTED_OPERATION);
    }

    binder::Status getSchedulingPolicy(gui::SchedulingPolicy* outPolicy) override {
        if (outPolicy == nullptr) {
            return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_ARGUMENT);
        }
        *outPolicy = gui::SchedulingPolicy();
        return binder::Status::ok();
    }

private:
    HostComposer* mComposer;
};

// ---------------------------------------------------------------------------
// AIDL composer: connection factory + BLAST tuning.
// ---------------------------------------------------------------------------

class HostAidlComposer final : public gui::ISurfaceComposerDefault {
public:
    explicit HostAidlComposer(HostComposer* composer) : mClient(sp<HostComposerClient>::make(composer)) {
        mBinder = sp<LocalInterfaceBinder>::make(this);
    }

    // The ISurfaceComposer implementation is not a BBinder, so hand out the
    // wrapper that binds it to the service manager.  IInterface::asBinder()
    // relies on this when ComposerServiceAIDL links to death.
    IBinder* onAsBinder() override { return mBinder.get(); }

    binder::Status createConnection(sp<gui::ISurfaceComposerClient>* outConnection) override {
        if (outConnection == nullptr) {
            return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_ARGUMENT);
        }
        *outConnection = mClient;
        return binder::Status::ok();
    }

    binder::Status getMaxAcquiredBufferCount(int32_t* outCount) override {
        if (outCount == nullptr) {
            return binder::Status::fromExceptionCode(binder::Status::EX_ILLEGAL_ARGUMENT);
        }
        *outCount = static_cast<int32_t>(kMaxAcquiredBuffers);
        return binder::Status::ok();
    }

private:
    // A local interface carrier: interface_cast<gui::ISurfaceComposer>() checks
    // queryLocalInterface() before falling back to a Bp proxy.
    class LocalInterfaceBinder final : public BBinder {
    public:
        explicit LocalInterfaceBinder(gui::ISurfaceComposer* impl) : mImpl(impl) {}

        sp<IInterface> queryLocalInterface(const String16& descriptor) override {
            if (descriptor == gui::ISurfaceComposer::descriptor) {
                return mImpl;
            }
            return nullptr;
        }

        const String16& getInterfaceDescriptor() const override {
            return gui::ISurfaceComposer::descriptor;
        }

    private:
        sp<gui::ISurfaceComposer> mImpl;
    };

    sp<gui::ISurfaceComposerClient> mClient;
    sp<LocalInterfaceBinder> mBinder;
};

// ---------------------------------------------------------------------------
// Legacy composer: Transaction::apply() lands here.
// ---------------------------------------------------------------------------

class HostLegacyComposer final : public BnSurfaceComposer {
public:
    explicit HostLegacyComposer(HostComposer* composer) : mComposer(composer) {}

    status_t setTransactionState(TransactionState&& state, const sp<IBinder>& /*applyToken*/) override {
        mComposer->enqueue(std::move(state));
        return NO_ERROR;
    }

    status_t registerGraphicBuffers(const gui::GraphicBuffersRegisterInfo& /*info*/) override {
        return NO_ERROR;
    }

    status_t unregisterGraphicBuffers(const gui::GraphicBuffersUnregisterInfo& /*info*/) override {
        return NO_ERROR;
    }

private:
    HostComposer* mComposer;
};

}  // namespace

bool hostComposerLayerInfo(ANativeWindow* window, int* x, int* y, int* width, int* height,
                           int* z, float* alpha, bool* shareable) {
    return gComposer != nullptr &&
            gComposer->layerInfoForWindow(window, x, y, width, height, z, alpha, shareable);
}

void hostComposerSetLayerShared(ANativeWindow* window, bool shared) {
    if (gComposer != nullptr) {
        gComposer->setLayerShared(window, shared);
    }
}

bool startHostComposer(int width, int height, const char* backend) {
    if (gComposer != nullptr) {
        return true;
    }
    if (width <= 0 || height <= 0) {
        ALOGE("invalid display size %dx%d", width, height);
        return false;
    }

    auto composer = std::make_unique<HostComposer>(width, height);
    if (!composer->init(backend)) {
        ALOGW("no display backend available; leaving the composer disabled");
        return false;
    }

    sp<IServiceManager> sm = defaultServiceManager();
    sp<HostLegacyComposer> legacy = sp<HostLegacyComposer>::make(composer.get());
    sp<HostAidlComposer> aidl = sp<HostAidlComposer>::make(composer.get());

    status_t legacyStatus = sm->addService(String16("SurfaceFlinger"), legacy);
    status_t aidlStatus =
            sm->addService(String16("SurfaceFlingerAIDL"), IInterface::asBinder(aidl));
    if (legacyStatus != NO_ERROR || aidlStatus != NO_ERROR) {
        ALOGE("unable to register composer services (%d/%d)", legacyStatus, aidlStatus);
        composer->shutdown();
        return false;
    }

    gServices[0] = legacy;
    gServices[1] = IInterface::asBinder(aidl);
    gComposer = composer.release();
    waylandHostSetLayerInfoCallback(&hostComposerLayerInfo);
    waylandHostSetLayerSharedCallback(&hostComposerSetLayerShared);

    // Tell the Java launcher that real surfaces are being presented, so it
    // does not also open a host window for the Stage 1 heap bitmap.
    setenv("DROIDLET_COMPOSER", "1", 1);

    ALOGI("registered SurfaceFlinger and SurfaceFlingerAIDL");
    return true;
}

void stopHostComposer() {
    if (gComposer == nullptr) {
        return;
    }
    gComposer->shutdown();
    delete gComposer;
    gComposer = nullptr;
    gServices[0].clear();
    gServices[1].clear();
}

void hostRemoveLayer(int layerId) {
    if (gComposer != nullptr) {
        gComposer->removeLayer(layerId);
    }
}

bool hostLayerPosition(int layerId, float* x, float* y) {
    return gComposer != nullptr && gComposer->layerPosition(layerId, x, y);
}

void hostFrameStats(uint64_t* latched, uint64_t* presented, uint64_t* dropped) {
    if (gComposer != nullptr) {
        gComposer->frameStats(latched, presented, dropped);
    }
}

bool hostResizeDisplay(int width, int height) {
    if (gComposer == nullptr) {
        return false;
    }
    gComposer->resize(width, height);

    if (!ensureWorkerAttached()) {
        return true;
    }
    JavaVM* vm = android::AndroidRuntime::getJavaVM();
    if (vm == nullptr) {
        return true;
    }
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK
            || env == nullptr) {
        return true;
    }
    jclass launcher = env->FindClass("android/app/DroidletLauncher");
    if (launcher == nullptr) {
        env->ExceptionClear();
        return true;
    }
    jmethodID method = env->GetStaticMethodID(launcher, "hostDisplayResized", "(II)V");
    if (method == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(launcher);
        return true;
    }
    env->CallStaticVoidMethod(launcher, method, static_cast<jint>(width),
                              static_cast<jint>(height));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(launcher);
    return true;
}

void requestHostClose() {
    if (!ensureWorkerAttached()) {
        return;
    }
    JavaVM* vm = android::AndroidRuntime::getJavaVM();
    if (vm == nullptr) {
        return;
    }
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK
            || env == nullptr) {
        return;
    }
    jclass launcher = env->FindClass("android/app/DroidletLauncher");
    if (launcher == nullptr) {
        env->ExceptionClear();
        return;
    }
    jmethodID method = env->GetStaticMethodID(launcher, "hostRequestClose", "()V");
    if (method == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(launcher);
        return;
    }
    env->CallStaticVoidMethod(launcher, method);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(launcher);
}

bool waitHostFrame(int timeoutMs) {
    return gComposer != nullptr && gComposer->waitForFrame(timeoutMs);
}

int copyHostFrame(uint32_t* pixels, int capacity) {
    return gComposer != nullptr ? gComposer->copyFrame(pixels, capacity) : -1;
}

int hostFrameWidth() { return gComposer != nullptr ? gComposer->frameWidth() : 0; }

int hostFrameHeight() { return gComposer != nullptr ? gComposer->frameHeight() : 0; }

}  // namespace droidlet
