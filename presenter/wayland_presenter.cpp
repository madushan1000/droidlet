/*
 * Wayland presenter: wl_compositor + xdg_wm_base + wl_shm.
 *
 * The pixel flow mirrors the eventual Android path in the useful way: the
 * client owns a buffer, writes pixels into it and commits it to a compositor.
 * The buffer here is a shared-memory pool instead of a gralloc/dmabuf buffer.
 */

#include "presenter.h"

#ifdef DROIDLET_HAVE_WAYLAND

#include <cerrno>
#include <cstdio>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <android/input.h>

#ifdef DROIDLET_HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#endif

#include "host_composer.h"
#include "input_injector.h"
#include "wayland_host.h"
#include "input_keys.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <linux/input-event-codes.h>

namespace droidlet {
namespace {

constexpr int kBufferCount = 2;
constexpr int kOverlayWidth = 720;
constexpr int kOverlayHeight = 240;
constexpr int kOverlayBufferCount = 3;
constexpr int kEffectBufferCount = 2;

int64_t monotonicMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// wl_keyboard.modifiers state bits from the core Wayland protocol.
constexpr uint32_t kKeyboardModShift = 1;
constexpr uint32_t kKeyboardModCtrl = 2;
constexpr uint32_t kKeyboardModAlt = 4;
constexpr uint32_t kKeyboardModLogo = 32;

struct ShmBuffer {
    wl_buffer* buffer = nullptr;
    void* data = nullptr;
    size_t size = 0;
    bool busy = false;
};

// A shm pool retired by a resize.  The compositor may still reference the
// wl_buffers (and therefore the mmap), so free it only after every buffer has
// been released.
struct RetiredPool {
    wl_shm_pool* pool = nullptr;
    void* data = nullptr;
    size_t size = 0;
    std::vector<std::unique_ptr<ShmBuffer>> buffers;
};

class WaylandPresenter final : public Presenter {
public:
    WaylandPresenter(int width, int height, const char* title)
        : mWidth(width), mHeight(height), mTitle(title ? title : "droidlet") {}

    ~WaylandPresenter() override { close(); }

    bool open() {
        std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
        if (!waylandHostInit()) {
            fprintf(stderr, "presenter: wayland: could not initialise host display\n");
            return false;
        }
        mDisplay = waylandHostDisplay();
        mCompositor = waylandHostCompositor();
        mShm = waylandHostShm();
        if (!mDisplay || !mCompositor || !mShm) {
            fprintf(stderr, "presenter: wayland: missing shared display objects\n");
            return false;
        }

        wl_registry* registry = wl_display_get_registry(mDisplay);
        static const wl_registry_listener registryListener = {
                registryGlobal, registryGlobalRemove};
        wl_registry_add_listener(registry, &registryListener, this);
        wl_display_roundtrip(mDisplay);
        wl_registry_destroy(registry);

        if (!mWmBase) {
            fprintf(stderr, "presenter: wayland: missing xdg_wm_base\n");
            close();
            return false;
        }

        mSurface = wl_compositor_create_surface(mCompositor);
        mXdgSurface = xdg_wm_base_get_xdg_surface(mWmBase, mSurface);
        static const xdg_surface_listener surfaceListener = {xdgSurfaceConfigure};
        xdg_surface_add_listener(mXdgSurface, &surfaceListener, this);

        mToplevel = xdg_surface_get_toplevel(mXdgSurface);
        static const xdg_toplevel_listener toplevelListener = {
                toplevelConfigure, toplevelClose};
        xdg_toplevel_add_listener(mToplevel, &toplevelListener, this);
        xdg_toplevel_set_title(mToplevel, mTitle.c_str());
        xdg_toplevel_set_app_id(mToplevel, "droidlet");

        // Ask the compositor to draw a titlebar/close button for us.
        if (mDecorationManager) {
            mToplevelDecoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
                    mDecorationManager, mToplevel);
            static const zxdg_toplevel_decoration_v1_listener decorationListener = {
                    decorationConfigure};
            zxdg_toplevel_decoration_v1_add_listener(
                    mToplevelDecoration, &decorationListener, this);
            zxdg_toplevel_decoration_v1_set_mode(
                    mToplevelDecoration,
                    ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        } else {
            fprintf(stderr, "presenter: wayland: xdg-decoration unavailable; "
                            "no server-side decorations\n");
        }

        waylandHostSetParentSurface(mSurface, mWidth, mHeight);
        wl_surface_commit(mSurface);
        if (!waitForConfigure()) {
            fprintf(stderr, "presenter: wayland: no xdg_surface configure\n");
            close();
            return false;
        }

        if (!createBuffers()) {
            close();
            return false;
        }
        xdg_surface_set_window_geometry(mXdgSurface, 0, 0, mWidth, mHeight);
        wl_surface_commit(mSurface);
        if (!createOverlaySurface()) {
            fprintf(stderr, "presenter: wayland: could not create overlay surface\n");
            close();
            return false;
        }
        if (std::getenv("DROIDLET_DISABLE_EFFECT") == nullptr &&
            !createEffectSurface(0, 0, mWidth, mHeight)) {
            fprintf(stderr, "presenter: wayland: could not create effect surface; "
                            "non-shared layers may be hidden\n");
        }
        fprintf(stderr, "presenter: wayland: window %dx%d title=\"%s\"\n",
                mWidth, mHeight, mTitle.c_str());
        return true;
    }

    bool present(const uint32_t* pixels, int width, int height) override {
        std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
        if (!mDisplay || width != mWidth || height != mHeight) {
            return false;
        }
        ShmBuffer* target = nullptr;
        for (auto& candidate : mBuffers) {
            if (!candidate->busy) {
                target = candidate.get();
                break;
            }
        }
        if (!target) {
            if (std::getenv("DROIDLET_PERF_OVERLAY_DEBUG") != nullptr) {
                static unsigned count = 0;
                if ((count++ % 60) == 0) {
                    fprintf(stderr, "presenter: wayland: no free main buffer (%u)\n", count);
                }
            }
            return false;
        }

        // Android's ARGB_8888 has 0xAARRGGBB in a uint32_t.  On little-endian
        // hosts that is the same byte order as WL_SHM_FORMAT_ARGB8888 /
        // XRGB8888, so a row copy is enough.
        const size_t rowBytes = static_cast<size_t>(width) * sizeof(uint32_t);
        for (int y = 0; y < height; ++y) {
            memcpy(static_cast<uint8_t*>(target->data) + y * rowBytes,
                   pixels + static_cast<size_t>(y) * width, rowBytes);
        }

        target->busy = true;
        wl_surface_attach(mSurface, target->buffer, 0, 0);
        wl_surface_damage_buffer(mSurface, 0, 0, width, height);
        wl_surface_commit(mSurface);
        wl_display_flush(mDisplay);
        return true;
    }

    bool supportsOverlay() const override { return mOverlaySubsurface != nullptr; }

    bool supportsEffect() const override { return mEffectSubsurface != nullptr; }

    bool presentEffect(const uint32_t* pixels, int frameWidth, int frameHeight,
                       int x, int y, int width, int height, int z) override {
        std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
        if (!mEffectSurface || pixels == nullptr || frameWidth <= 0 || frameHeight <= 0 ||
            width <= 0 || height <= 0) {
            return false;
        }
        if (x < 0 || y < 0 || x + width > frameWidth || y + height > frameHeight) {
            return false;
        }
        // A full-screen transparent effect surface makes the compositor
        // composite the whole window for every video frame.  Keep the
        // subsurface at the bounds of the layers that actually need it (the
        // video rect is 1248x240, not 1280x800).
        if (x != mEffectX || y != mEffectY || width != mEffectWidth ||
            height != mEffectHeight) {
            destroyEffectSurface();
            if (!createEffectSurface(x, y, width, height)) {
                return false;
            }
            // wl_subsurface position is applied on the parent surface commit.
            // The parent frame is now skipped when it has not changed, so
            // commit it explicitly or the effect stays at its old position.
            wl_surface_commit(mSurface);
        }
        int slot = -1;
        for (int i = 0; i < kEffectBufferCount; ++i) {
            if (!mEffectBusy[i] && mEffectBuffers[i] != nullptr) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            return false;
        }
        const size_t rowBytes = static_cast<size_t>(width) * 4;
        for (int row = 0; row < height; ++row) {
            memcpy(static_cast<uint8_t*>(mEffectData[slot]) + static_cast<size_t>(row) * rowBytes,
                   pixels + static_cast<size_t>(y + row) * frameWidth + x, rowBytes);
        }
        waylandHostSetEffectZ(z);
        mEffectBusy[slot] = true;
        wl_surface_attach(mEffectSurface, mEffectBuffers[slot], 0, 0);
        wl_surface_damage_buffer(mEffectSurface, 0, 0, width, height);
        wl_surface_commit(mEffectSurface);
        wl_display_flush(mDisplay);
        return true;
    }

    bool presentOverlay(const uint32_t* pixels, int width, int height) override {
        std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
        if (!mOverlaySurface || pixels == nullptr) {
            return false;
        }
        if (width <= 0 || height <= 0 || width > kOverlayWidth || height > kOverlayHeight) {
            return false;
        }
        int slot = -1;
        for (int i = 0; i < kOverlayBufferCount; ++i) {
            if (!mOverlayBusy[i] && mOverlayBuffers[i] != nullptr) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            if (std::getenv("DROIDLET_PERF_OVERLAY_DEBUG") != nullptr) {
                static unsigned count = 0;
                if ((count++ % 60) == 0) {
                    fprintf(stderr, "presenter: wayland: overlay busy/skipped (%u)\n", count);
                }
            }
            return false;
        }
        for (int y = 0; y < height; ++y) {
            memcpy(static_cast<uint8_t*>(mOverlayData[slot]) + y * kOverlayWidth * 4,
                   pixels + static_cast<size_t>(y) * width, static_cast<size_t>(width) * 4);
        }
        waylandHostRaiseOverlay();
        mOverlayBusy[slot] = true;
        wl_surface_attach(mOverlaySurface, mOverlayBuffers[slot], 0, 0);
        wl_surface_damage_buffer(mOverlaySurface, 0, 0, width, height);
        wl_surface_commit(mOverlaySurface);
        wl_display_flush(mDisplay);
        return true;
    }

    void poll() override {
        std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
        if (!mDisplay) {
            return;
        }
        wl_display_dispatch_pending(mDisplay);
        while (wl_display_prepare_read(mDisplay) != 0) {
            wl_display_dispatch_pending(mDisplay);
        }
        wl_display_flush(mDisplay);

        pollfd pfd{wl_display_get_fd(mDisplay), POLLIN, 0};
        int ready = ::poll(&pfd, 1, 0);
        if (ready > 0 && (pfd.revents & POLLIN)) {
            wl_display_read_events(mDisplay);
        } else {
            wl_display_cancel_read(mDisplay);
        }
        wl_display_dispatch_pending(mDisplay);
        freeRetiredPools();
        applyPendingResize(false /* force */);
    }

    const char* name() const override { return "wayland"; }

private:
    static void registryGlobal(void* data, wl_registry* registry, uint32_t name,
                               const char* interface, uint32_t version) {
        auto* self = static_cast<WaylandPresenter*>(data);
        if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
            self->mWmBase = static_cast<xdg_wm_base*>(
                    wl_registry_bind(registry, name, &xdg_wm_base_interface,
                                     version < 2 ? version : 2));
            static const xdg_wm_base_listener wmBaseListener = {wmBasePing};
            xdg_wm_base_add_listener(self->mWmBase, &wmBaseListener, self);
        } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
            self->mDecorationManager = static_cast<zxdg_decoration_manager_v1*>(
                    wl_registry_bind(registry, name,
                                     &zxdg_decoration_manager_v1_interface, 1));
        } else if (strcmp(interface, wl_seat_interface.name) == 0) {
            self->mSeat = static_cast<wl_seat*>(
                    wl_registry_bind(registry, name, &wl_seat_interface,
                                     version < 7 ? version : 7));
            static const wl_seat_listener seatListener = {seatCapabilities, seatName};
            wl_seat_add_listener(self->mSeat, &seatListener, self);
        }
    }

    static void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

    static void decorationConfigure(void*, zxdg_toplevel_decoration_v1*, uint32_t mode) {
        fprintf(stderr, "presenter: wayland: decoration mode=%s\n",
                mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                        ? "server-side"
                        : "client-side");
    }

    // -- seat and input devices ---------------------------------------------

    static void seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities) {
        auto* self = static_cast<WaylandPresenter*>(data);
        if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !self->mPointer) {
            self->mPointer = wl_seat_get_pointer(seat);
            // Every slot must be non-null: libwayland invokes the callback
            // directly when the compositor sends the event.  In particular,
            // pointer.frame is sent after ordinary motion.
            static const wl_pointer_listener listener = {
                    pointerEnter, pointerLeave, pointerMotion, pointerButton, pointerAxis,
                    pointerFrame, pointerAxisSource, pointerAxisStop, pointerAxisDiscrete,
                    pointerAxisValue120, pointerAxisRelativeDirection};
            wl_pointer_add_listener(self->mPointer, &listener, self);
        } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && self->mPointer) {
            wl_pointer_release(self->mPointer);
            self->mPointer = nullptr;
        }

        if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !self->mKeyboard) {
            self->mKeyboard = wl_seat_get_keyboard(seat);
            static const wl_keyboard_listener listener = {
                    keyboardKeymap, keyboardEnter, keyboardLeave, keyboardKey,
                    keyboardModifiers, keyboardRepeatInfo};
            wl_keyboard_add_listener(self->mKeyboard, &listener, self);
        } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && self->mKeyboard) {
            wl_keyboard_release(self->mKeyboard);
            self->mKeyboard = nullptr;
        }

        if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !self->mTouch) {
            self->mTouch = wl_seat_get_touch(seat);
            static const wl_touch_listener listener = {
                    touchDown, touchUp, touchMotion, touchFrame, touchCancel,
                    touchShape, touchOrientation};
            wl_touch_add_listener(self->mTouch, &listener, self);
        } else if (!(capabilities & WL_SEAT_CAPABILITY_TOUCH) && self->mTouch) {
            wl_touch_release(self->mTouch);
            self->mTouch = nullptr;
        }
    }

    static void seatName(void*, wl_seat*, const char*) {}

    static void pointerEnter(void* data, wl_pointer*, uint32_t, wl_surface*,
                             wl_fixed_t x, wl_fixed_t y) {
        auto* self = static_cast<WaylandPresenter*>(data);
        self->mPointerX = wl_fixed_to_double(x);
        self->mPointerY = wl_fixed_to_double(y);
        if (const char* debug = std::getenv("DROIDLET_POINTER_DEBUG");
                debug != nullptr && debug[0] == '1') {
            std::fprintf(stderr, "presenter: pointer enter %.1f,%.1f down=%d\n",
                         self->mPointerX, self->mPointerY, self->mPointerDown);
        }
        if (self->mPointerDown) {
            // The pointer left and came back (focus change, window switch)
            // without a release reaching us.  End the app's stale gesture so
            // the next motion cannot keep steering toward an old target.
            injectTouchUp(self->mPointerX, self->mPointerY, monotonicMs());
            self->mPointerDown = false;
            self->mNeedReleaseConfirm = false;
        }
        // Entering the window is a desktop-style focus request.  If the app
        // paused after losing focus, this resumes it.
        injectFocus(true);
    }

    static void pointerLeave(void* data, wl_pointer*, uint32_t, wl_surface*) {
        auto* self = static_cast<WaylandPresenter*>(data);
        if (const char* debug = std::getenv("DROIDLET_POINTER_DEBUG");
                debug != nullptr && debug[0] == '1') {
            std::fprintf(stderr, "presenter: pointer leave at %.1f,%.1f down=%d\n",
                         self->mPointerX, self->mPointerY, self->mPointerDown);
        }
        if (self->mPointerDown) {
            // Send UP, not CANCEL: NativeActivity games such as Endless Tunnel
            // treat CANCEL as a pointer move and keep steering toward the last
            // target.  An UP ends the gesture so a later press can steer again.
            injectTouchUp(self->mPointerX, self->mPointerY, monotonicMs());
            self->mPointerDown = false;
            self->mNeedReleaseConfirm = true;
        }
    }

    static void pointerMotion(void* data, wl_pointer*, uint32_t time,
                              wl_fixed_t x, wl_fixed_t y) {
        auto* self = static_cast<WaylandPresenter*>(data);
        self->mPointerX = wl_fixed_to_double(x);
        self->mPointerY = wl_fixed_to_double(y);
        if (const char* debug = std::getenv("DROIDLET_POINTER_DEBUG");
                debug != nullptr && debug[0] == '1') {
            std::fprintf(stderr, "presenter: pointer motion %.1f,%.1f down=%d\n",
                         self->mPointerX, self->mPointerY, self->mPointerDown);
        }
        if (self->mPointerDown) {
            injectTouchMove(self->mPointerX, self->mPointerY, time);
        } else {
            if (self->mNeedReleaseConfirm) {
                // The host released the button, but the app may not have seen
                // it (focus change, dropped channel event).  NativeActivity
                // games then keep their old steering anchor and appear stuck
                // in a corner.  Only send the confirmation to the window that
                // saw the press: a popup/dialog may have opened since then,
                // and a stray UP outside its bounds would cancel it.
                if (activeInputChannelId() == self->mDownInputChannel) {
                    injectTouchUp(self->mPointerX, self->mPointerY, time);
                }
                self->mNeedReleaseConfirm = false;
            }
            injectHoverMove(self->mPointerX, self->mPointerY, time);
        }
    }

    static void pointerButton(void* data, wl_pointer*, uint32_t, uint32_t time,
                              uint32_t button, uint32_t state) {
        if (button != BTN_LEFT) {
            return;
        }
        auto* self = static_cast<WaylandPresenter*>(data);
        if (const char* debug = std::getenv("DROIDLET_POINTER_DEBUG");
                debug != nullptr && debug[0] == '1') {
            std::fprintf(stderr, "presenter: pointer button state=%u at %.1f,%.1f\n", state,
                         self->mPointerX, self->mPointerY);
        }
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
            self->mPointerDown = true;
            self->mNeedReleaseConfirm = false;
            self->mDownInputChannel = activeInputChannelId();
            injectFocus(true);
            injectTouchDown(self->mPointerX, self->mPointerY, time);
        } else {
            injectTouchUp(self->mPointerX, self->mPointerY, time);
            self->mPointerDown = false;
            self->mNeedReleaseConfirm = true;
        }
    }

    static void pointerAxis(void* data, wl_pointer*, uint32_t time, uint32_t axis,
                            wl_fixed_t value) {
        auto* self = static_cast<WaylandPresenter*>(data);
        const double raw = wl_fixed_to_double(value);
        if (raw == 0.0) {
            return;
        }
        // A wheel detent is 120 units.  Trackpads may send smaller deltas, so
        // fall back to one step for any non-zero value.
        double steps = raw / 120.0;
        if (steps > -0.01 && steps < 0.01) {
            steps = raw > 0 ? 1.0 : -1.0;
        }
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            // Wayland positive is wheel-down; Android AXIS_VSCROLL positive
            // scrolls the viewport up.
            injectScroll(self->mPointerX, self->mPointerY, 0.0f,
                         static_cast<float>(-steps), time);
        } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
            injectScroll(self->mPointerX, self->mPointerY,
                         static_cast<float>(steps), 0.0f, time);
        }
    }

    static void pointerFrame(void*, wl_pointer*) {}

    static void pointerAxisSource(void*, wl_pointer*, uint32_t) {}

    static void pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}

    static void pointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}

    static void pointerAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}

    static void pointerAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}

    static void keyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd,
                               uint32_t size) {
#ifdef DROIDLET_HAVE_XKBCOMMON
        auto* self = static_cast<WaylandPresenter*>(data);
        if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && fd >= 0 && size > 0) {
            void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped != MAP_FAILED) {
                if (!self->mXkbContext) {
                    self->mXkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
                }
                xkb_keymap* keymap = nullptr;
                if (self->mXkbContext) {
                    keymap = xkb_keymap_new_from_string(
                            self->mXkbContext, static_cast<const char*>(mapped),
                            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
                }
                munmap(mapped, size);
                if (keymap) {
                    xkb_state* state = xkb_state_new(keymap);
                    if (state) {
                        if (self->mXkbState) {
                            xkb_state_unref(self->mXkbState);
                        }
                        if (self->mXkbKeymap) {
                            xkb_keymap_unref(self->mXkbKeymap);
                        }
                        self->mXkbKeymap = keymap;
                        self->mXkbState = state;
                    } else {
                        xkb_keymap_unref(keymap);
                    }
                }
            }
        }
#endif
        if (fd >= 0) {
            ::close(fd);
        }
    }

    static void keyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {
        // The compositor granted keyboard focus (a click, a window switch).
        // Re-assert app focus in case it paused after losing it.
        injectFocus(true);
    }

    static void keyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

    static void keyboardKey(void* data, wl_keyboard*, uint32_t, uint32_t time,
                            uint32_t key, uint32_t state) {
        auto* self = static_cast<WaylandPresenter*>(data);
        uint32_t extraMeta = 0;
        int keyCode = 0;
#ifdef DROIDLET_HAVE_XKBCOMMON
        if (self->mXkbState) {
            const xkb_keysym_t sym =
                    xkb_state_key_get_one_sym(self->mXkbState, key + 8);
            keyCode = androidKeyCodeFromKeysym(static_cast<uint32_t>(sym), &extraMeta);
            if (keyCode == 0) {
                // The host keymap resolved a symbol we cannot represent.
                // Falling back to the physical key would type the wrong
                // character (for example an accented letter or a dead key).
                return;
            }
        }
#endif
        if (keyCode == 0) {
            keyCode = androidKeyCodeFromEvdev(static_cast<int>(key));
        }
        if (keyCode == 0) {
            return;
        }
        const int action = state == WL_KEYBOARD_KEY_STATE_PRESSED
                ? AKEY_EVENT_ACTION_DOWN
                : AKEY_EVENT_ACTION_UP;
        injectKey(action, keyCode, static_cast<int>(key),
                  self->mKeyboardMeta | extraMeta, time);
    }

    static void keyboardModifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed,
                                  uint32_t latched, uint32_t locked, uint32_t group) {
        auto* self = static_cast<WaylandPresenter*>(data);
#ifdef DROIDLET_HAVE_XKBCOMMON
        if (self->mXkbState) {
            xkb_state_update_mask(self->mXkbState, depressed, latched, locked,
                                  0, 0, group);
        }
#endif
        const uint32_t modifiers = depressed | latched | locked;
        uint32_t meta = 0;
        if (modifiers & kKeyboardModShift) meta |= kMetaShiftOn;
        if (modifiers & kKeyboardModCtrl) meta |= kMetaCtrlOn;
        if (modifiers & kKeyboardModAlt) meta |= kMetaAltOn;
        if (modifiers & kKeyboardModLogo) meta |= kMetaMetaOn;
        self->mKeyboardMeta = meta;
    }

    static void keyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

    static void touchDown(void* data, wl_touch*, uint32_t, uint32_t time,
                          wl_surface*, int32_t id, wl_fixed_t x, wl_fixed_t y) {
        auto* self = static_cast<WaylandPresenter*>(data);
        if (self->mTouchId >= 0) {
            return;
        }
        self->mTouchId = id;
        self->mTouchX = wl_fixed_to_double(x);
        self->mTouchY = wl_fixed_to_double(y);
        injectFocus(true);
        injectTouchDown(self->mTouchX, self->mTouchY, time);
    }

    static void touchUp(void* data, wl_touch*, uint32_t, uint32_t time, int32_t id) {
        auto* self = static_cast<WaylandPresenter*>(data);
        if (id != self->mTouchId) {
            return;
        }
        injectTouchUp(self->mTouchX, self->mTouchY, time);
        self->mTouchId = -1;
    }

    static void touchMotion(void* data, wl_touch*, uint32_t time, int32_t id,
                            wl_fixed_t x, wl_fixed_t y) {
        auto* self = static_cast<WaylandPresenter*>(data);
        if (id != self->mTouchId) {
            return;
        }
        self->mTouchX = wl_fixed_to_double(x);
        self->mTouchY = wl_fixed_to_double(y);
        injectTouchMove(self->mTouchX, self->mTouchY, time);
    }

    static void touchFrame(void*, wl_touch*) {}

    static void touchShape(void*, wl_touch*, int32_t, wl_fixed_t, wl_fixed_t) {}

    static void touchOrientation(void*, wl_touch*, int32_t, wl_fixed_t) {}

    static void touchCancel(void* data, wl_touch*) {
        auto* self = static_cast<WaylandPresenter*>(data);
        if (self->mTouchId < 0) {
            return;
        }
        // Treat a cancel like a release: the game maps CANCEL to a pointer
        // move and would otherwise stay stuck steering at its last target.
        injectTouchUp(self->mTouchX, self->mTouchY, monotonicMs());
        self->mTouchId = -1;
    }

    static void wmBasePing(void*, xdg_wm_base* wmBase, uint32_t serial) {
        xdg_wm_base_pong(wmBase, serial);
    }

    static void xdgSurfaceConfigure(void* data, xdg_surface* surface, uint32_t serial) {
        auto* self = static_cast<WaylandPresenter*>(data);
        xdg_surface_ack_configure(surface, serial);
        self->mConfigured = true;
        // The final configure (RESIZING cleared) is applied immediately; a
        // mid-drag one may be throttled and retried from poll().
        self->applyPendingResize(!self->mInteractiveResize);
    }

    static void toplevelConfigure(void* data, xdg_toplevel*, int32_t width, int32_t height,
                                  wl_array* states) {
        auto* self = static_cast<WaylandPresenter*>(data);
        if (width > 0 && height > 0) {
            self->mPendingWidth = width;
            self->mPendingHeight = height;
        }
        bool resizing = false;
        if (states != nullptr && states->data != nullptr) {
            const auto* state = static_cast<const uint32_t*>(states->data);
            const size_t count = states->size / sizeof(uint32_t);
            for (size_t i = 0; i < count; ++i) {
                if (state[i] == XDG_TOPLEVEL_STATE_RESIZING) {
                    resizing = true;
                }
            }
        }
        self->mInteractiveResize = resizing;
    }

    static void toplevelClose(void*, xdg_toplevel*) {
        fprintf(stderr, "presenter: wayland: compositor requested close\n");
        requestHostClose();
    }

    static void bufferRelease(void* data, wl_buffer*) {
        static_cast<ShmBuffer*>(data)->busy = false;
    }

    static void overlayBufferRelease(void* data, wl_buffer*) {
        *static_cast<bool*>(data) = false;
    }

    static void effectBufferRelease(void* data, wl_buffer*) {
        *static_cast<bool*>(data) = false;
    }

    bool waitForConfigure() {
        // A compositor that is ready sends configure during the first
        // roundtrip.  Poll for a bounded time so a broken compositor cannot
        // hang the launcher.
        for (int i = 0; i < 100 && !mConfigured; ++i) {
            if (wl_display_roundtrip(mDisplay) < 0) {
                return false;
            }
        }
        return mConfigured;
    }

    static int createShmFile(size_t size) {
        char name[64];
        snprintf(name, sizeof(name), "droidlet-%d", static_cast<int>(getpid()));
        int fd = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (fd < 0) {
            fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
            if (fd >= 0) {
                shm_unlink(name);
            }
        }
        if (fd < 0) {
            return -1;
        }
        if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    bool createOverlaySurface() {
        if (!mDisplay || !mCompositor || !mShm || waylandHostParentSurface() == nullptr ||
                waylandHostSubcompositor() == nullptr) {
            return false;
        }
        mOverlaySurface = wl_compositor_create_surface(mCompositor);
        if (mOverlaySurface == nullptr) {
            return false;
        }
        mOverlaySubsurface = wl_subcompositor_get_subsurface(
                waylandHostSubcompositor(), mOverlaySurface, waylandHostParentSurface());
        if (mOverlaySubsurface == nullptr) {
            wl_surface_destroy(mOverlaySurface);
            mOverlaySurface = nullptr;
            return false;
        }
        wl_subsurface_set_position(mOverlaySubsurface, 0, 0);
        wl_subsurface_set_desync(mOverlaySubsurface);
        wl_region* empty = wl_compositor_create_region(mCompositor);
        if (empty != nullptr) {
            wl_surface_set_input_region(mOverlaySurface, empty);
            wl_region_destroy(empty);
        }

        const size_t stride = static_cast<size_t>(kOverlayWidth) * 4;
        const size_t bufferSize = stride * kOverlayHeight;
        mOverlayPoolSize = bufferSize * kOverlayBufferCount;
        int fd = createShmFile(mOverlayPoolSize);
        if (fd < 0) {
            return false;
        }
        mOverlayMapping = mmap(nullptr, mOverlayPoolSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mOverlayMapping == MAP_FAILED) {
            mOverlayMapping = nullptr;
            ::close(fd);
            return false;
        }
        memset(mOverlayMapping, 0, mOverlayPoolSize);
        mOverlayPool = wl_shm_create_pool(mShm, fd, static_cast<int32_t>(mOverlayPoolSize));
        ::close(fd);
        if (mOverlayPool == nullptr) {
            return false;
        }
        static const wl_buffer_listener listener = {overlayBufferRelease};
        for (int i = 0; i < kOverlayBufferCount; ++i) {
            mOverlayData[i] = static_cast<uint8_t*>(mOverlayMapping) + i * bufferSize;
            mOverlayBuffers[i] = wl_shm_pool_create_buffer(
                    mOverlayPool, static_cast<int32_t>(i * bufferSize), kOverlayWidth,
                    kOverlayHeight, static_cast<int32_t>(stride), WL_SHM_FORMAT_ARGB8888);
            if (mOverlayBuffers[i] == nullptr) {
                return false;
            }
            wl_buffer_add_listener(mOverlayBuffers[i], &listener, &mOverlayBusy[i]);
        }
        // Do not commit an initial transparent buffer.  The overlay is created
        // before the app surfaces; committing here can leave the buffer
        // unreleased under some compositors (notably headless Weston) because
        // the surface is not yet raised/visible.  presentOverlay() commits it
        // after the app subsurfaces exist and the overlay has been raised.
        waylandHostSetOverlaySubsurface(mOverlaySubsurface, mOverlaySurface);
        return true;
    }

    void destroyEffectSurface() {
        if (mEffectSubsurface) {
            waylandHostSetEffectSubsurface(nullptr, nullptr, 0);
            wl_subsurface_destroy(mEffectSubsurface);
            mEffectSubsurface = nullptr;
        }
        for (int i = 0; i < kEffectBufferCount; ++i) {
            if (mEffectBuffers[i]) {
                wl_buffer_destroy(mEffectBuffers[i]);
                mEffectBuffers[i] = nullptr;
            }
            mEffectData[i] = nullptr;
            mEffectBusy[i] = false;
        }
        if (mEffectPool) {
            wl_shm_pool_destroy(mEffectPool);
            mEffectPool = nullptr;
        }
        if (mEffectMapping) {
            munmap(mEffectMapping, mEffectPoolSize);
            mEffectMapping = nullptr;
            mEffectPoolSize = 0;
        }
        if (mEffectSurface) {
            wl_surface_destroy(mEffectSurface);
            mEffectSurface = nullptr;
        }
    }

    bool createEffectSurface(int x, int y, int width, int height) {
        if (!mDisplay || !mCompositor || !mShm || waylandHostParentSurface() == nullptr ||
                waylandHostSubcompositor() == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        mEffectSurface = wl_compositor_create_surface(mCompositor);
        if (mEffectSurface == nullptr) {
            return false;
        }
        mEffectSubsurface = wl_subcompositor_get_subsurface(
                waylandHostSubcompositor(), mEffectSurface, waylandHostParentSurface());
        if (mEffectSubsurface == nullptr) {
            destroyEffectSurface();
            return false;
        }
        wl_subsurface_set_position(mEffectSubsurface, x, y);
        wl_subsurface_set_desync(mEffectSubsurface);
        wl_region* empty = wl_compositor_create_region(mCompositor);
        if (empty != nullptr) {
            wl_surface_set_input_region(mEffectSurface, empty);
            wl_region_destroy(empty);
        }
        mEffectX = x;
        mEffectY = y;
        mEffectWidth = width;
        mEffectHeight = height;

        const size_t stride = static_cast<size_t>(width) * 4;
        const size_t bufferSize = stride * static_cast<size_t>(height);
        mEffectPoolSize = bufferSize * kEffectBufferCount;
        int fd = createShmFile(mEffectPoolSize);
        if (fd < 0) {
            destroyEffectSurface();
            return false;
        }
        mEffectMapping = mmap(nullptr, mEffectPoolSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mEffectMapping == MAP_FAILED) {
            mEffectMapping = nullptr;
            ::close(fd);
            destroyEffectSurface();
            return false;
        }
        memset(mEffectMapping, 0, mEffectPoolSize);
        mEffectPool = wl_shm_create_pool(mShm, fd, static_cast<int32_t>(mEffectPoolSize));
        ::close(fd);
        if (mEffectPool == nullptr) {
            destroyEffectSurface();
            return false;
        }
        static const wl_buffer_listener listener = {effectBufferRelease};
        for (int i = 0; i < kEffectBufferCount; ++i) {
            mEffectData[i] = static_cast<uint8_t*>(mEffectMapping) + i * bufferSize;
            mEffectBuffers[i] = wl_shm_pool_create_buffer(
                    mEffectPool, static_cast<int32_t>(i * bufferSize), width, height,
                    static_cast<int32_t>(stride), WL_SHM_FORMAT_ARGB8888);
            if (mEffectBuffers[i] == nullptr) {
                destroyEffectSurface();
                return false;
            }
            wl_buffer_add_listener(mEffectBuffers[i], &listener, &mEffectBusy[i]);
        }
        waylandHostSetEffectSubsurface(mEffectSubsurface, mEffectSurface, 0);
        return true;
    }

    bool createBuffers() {
        const size_t stride = static_cast<size_t>(mWidth) * sizeof(uint32_t);
        const size_t size = stride * static_cast<size_t>(mHeight);
        int fd = createShmFile(size * kBufferCount);
        if (fd < 0) {
            fprintf(stderr, "presenter: wayland: shm allocation failed: %s\n",
                    strerror(errno));
            return false;
        }
        void* mapping = mmap(nullptr, size * kBufferCount, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED) {
            fprintf(stderr, "presenter: wayland: shm mmap failed: %s\n", strerror(errno));
            ::close(fd);
            return false;
        }
        mPoolData = mapping;
        mPoolSize = size * kBufferCount;

        mPool = wl_shm_create_pool(mShm, fd, static_cast<int32_t>(mPoolSize));
        ::close(fd);

        mBuffers.clear();
        mBuffers.reserve(kBufferCount);
        for (int i = 0; i < kBufferCount; ++i) {
            auto entry = std::make_unique<ShmBuffer>();
            entry->data = static_cast<uint8_t*>(mapping) + i * size;
            entry->size = size;
            entry->buffer = wl_shm_pool_create_buffer(
                    mPool, static_cast<int32_t>(i * size), mWidth, mHeight,
                    static_cast<int32_t>(stride), WL_SHM_FORMAT_XRGB8888);
            if (!entry->buffer) {
                return false;
            }
            static const wl_buffer_listener listener = {bufferRelease};
            wl_buffer_add_listener(entry->buffer, &listener, entry.get());
            mBuffers.push_back(std::move(entry));
        }
        return true;
    }

    /*
     * Move the current shm pool aside so a resize can install a new one
     * without freeing memory the compositor may still be reading.
     */
    void retireBuffers() {
        if (mBuffers.empty() && mPool == nullptr) {
            return;
        }
        auto retired = std::make_unique<RetiredPool>();
        retired->pool = mPool;
        retired->data = mPoolData;
        retired->size = mPoolSize;
        retired->buffers = std::move(mBuffers);
        mRetiredPools.push_back(std::move(retired));
        mBuffers.clear();
        mPool = nullptr;
        mPoolData = nullptr;
        mPoolSize = 0;
    }

    void freeRetiredPools() {
        for (auto it = mRetiredPools.begin(); it != mRetiredPools.end();) {
            bool busy = false;
            for (auto& buffer : (*it)->buffers) {
                if (buffer->busy) {
                    busy = true;
                    break;
                }
            }
            if (busy) {
                ++it;
                continue;
            }
            for (auto& buffer : (*it)->buffers) {
                if (buffer->buffer) {
                    wl_buffer_destroy(buffer->buffer);
                }
            }
            if ((*it)->pool) {
                wl_shm_pool_destroy((*it)->pool);
            }
            if ((*it)->data) {
                munmap((*it)->data, (*it)->size);
            }
            it = mRetiredPools.erase(it);
        }
    }

    /*
     * Apply the latest configure size.  During an interactive drag the
     * compositor sends many configures; throttle the display resizes but do
     * not lose the last one: poll() retries it once the throttle window has
     * elapsed.  A lost final size used to leave the parent surface smaller
     * than the compositor's window, showing the desktop through a strip at
     * the edge.
     */
    void applyPendingResize(bool force) {
        if (mPendingWidth <= 0 || mPendingHeight <= 0) {
            return;
        }
        if (mBuffers.empty()) {
            mWidth = mPendingWidth;
            mHeight = mPendingHeight;
            waylandHostSetParentSurface(mSurface, mWidth, mHeight);
            mPendingWidth = 0;
            mPendingHeight = 0;
            return;
        }
        if (mPendingWidth == mWidth && mPendingHeight == mHeight) {
            mPendingWidth = 0;
            mPendingHeight = 0;
            return;
        }
        if (!force) {
            const auto now = std::chrono::steady_clock::now();
            if (now - mLastResize < std::chrono::milliseconds(100)) {
                return;
            }
        }
        const int width = mPendingWidth;
        const int height = mPendingHeight;
        mPendingWidth = 0;
        mPendingHeight = 0;
        droidlet::hostResizeDisplay(width, height);
    }

    bool resize(int width, int height) override {
        if (width <= 0 || height <= 0 || (width == mWidth && height == mHeight)) {
            return true;
        }
        fprintf(stderr, "presenter: wayland: resize %dx%d -> %dx%d\n", mWidth, mHeight,
                width, height);
        retireBuffers();
        mWidth = width;
        mHeight = height;
        // The effect surface is recreated on demand by presentEffect(); its old
        // buffers may still be referenced, so keep them alive in the same way.
        destroyEffectSurface();
        waylandHostSetParentSurface(mSurface, mWidth, mHeight);
        if (!createBuffers()) {
            fprintf(stderr, "presenter: wayland: could not resize buffers\n");
            return false;
        }
        // Tell the compositor the content size explicitly.  Without window
        // geometry, server-side decoration compositors may keep the frame at
        // the last configured size while the surface is still smaller.
        if (mXdgSurface != nullptr) {
            xdg_surface_set_window_geometry(mXdgSurface, 0, 0, mWidth, mHeight);
            wl_surface_commit(mSurface);
        }
        mLastResize = std::chrono::steady_clock::now();
        return true;
    }

    void close() {
        std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
        for (auto& entry : mBuffers) {
            if (entry->buffer) {
                wl_buffer_destroy(entry->buffer);
                entry->buffer = nullptr;
            }
        }
        mBuffers.clear();
        if (mPool) {
            wl_shm_pool_destroy(mPool);
            mPool = nullptr;
        }
        if (mPoolData) {
            munmap(mPoolData, mPoolSize);
            mPoolData = nullptr;
            mPoolSize = 0;
        }
        for (auto& retired : mRetiredPools) {
            for (auto& buffer : retired->buffers) {
                if (buffer->buffer) {
                    wl_buffer_destroy(buffer->buffer);
                }
            }
            if (retired->pool) {
                wl_shm_pool_destroy(retired->pool);
            }
            if (retired->data) {
                munmap(retired->data, retired->size);
            }
        }
        mRetiredPools.clear();
        if (mToplevelDecoration) {
            zxdg_toplevel_decoration_v1_destroy(mToplevelDecoration);
            mToplevelDecoration = nullptr;
        }
        if (mToplevel) {
            xdg_toplevel_destroy(mToplevel);
            mToplevel = nullptr;
        }
        if (mDecorationManager) {
            zxdg_decoration_manager_v1_destroy(mDecorationManager);
            mDecorationManager = nullptr;
        }
        if (mXdgSurface) {
            xdg_surface_destroy(mXdgSurface);
            mXdgSurface = nullptr;
        }
        if (mPointer) {
            wl_pointer_release(mPointer);
            mPointer = nullptr;
        }
#ifdef DROIDLET_HAVE_XKBCOMMON
        if (mXkbState) {
            xkb_state_unref(mXkbState);
            mXkbState = nullptr;
        }
        if (mXkbKeymap) {
            xkb_keymap_unref(mXkbKeymap);
            mXkbKeymap = nullptr;
        }
        if (mXkbContext) {
            xkb_context_unref(mXkbContext);
            mXkbContext = nullptr;
        }
#endif
        if (mKeyboard) {
            wl_keyboard_release(mKeyboard);
            mKeyboard = nullptr;
        }
        if (mTouch) {
            wl_touch_release(mTouch);
            mTouch = nullptr;
        }
        if (mSeat) {
            wl_seat_release(mSeat);
            mSeat = nullptr;
        }
        destroyEffectSurface();
        if (mOverlaySubsurface) {
            waylandHostSetOverlaySubsurface(nullptr, nullptr);
            wl_subsurface_destroy(mOverlaySubsurface);
            mOverlaySubsurface = nullptr;
        }
        for (int i = 0; i < kOverlayBufferCount; ++i) {
            if (mOverlayBuffers[i]) {
                wl_buffer_destroy(mOverlayBuffers[i]);
                mOverlayBuffers[i] = nullptr;
            }
            mOverlayData[i] = nullptr;
            mOverlayBusy[i] = false;
        }
        if (mOverlayPool) {
            wl_shm_pool_destroy(mOverlayPool);
            mOverlayPool = nullptr;
        }
        if (mOverlayMapping) {
            munmap(mOverlayMapping, mOverlayPoolSize);
            mOverlayMapping = nullptr;
            mOverlayPoolSize = 0;
        }
        if (mOverlaySurface) {
            wl_surface_destroy(mOverlaySurface);
            mOverlaySurface = nullptr;
        }
        if (mSurface) {
            wl_surface_destroy(mSurface);
            mSurface = nullptr;
        }
        if (mWmBase) {
            xdg_wm_base_destroy(mWmBase);
            mWmBase = nullptr;
        }
        // mDisplay/mCompositor/mShm belong to the shared wayland_host context
        // and stay alive for the EGL renderers.
        mShm = nullptr;
        mCompositor = nullptr;
        mDisplay = nullptr;
    }

    int mWidth;
    int mHeight;
    std::string mTitle;
    wl_display* mDisplay = nullptr;
    wl_compositor* mCompositor = nullptr;
    wl_shm* mShm = nullptr;
    xdg_wm_base* mWmBase = nullptr;
    wl_seat* mSeat = nullptr;
    wl_pointer* mPointer = nullptr;
    wl_keyboard* mKeyboard = nullptr;
    wl_touch* mTouch = nullptr;
    wl_surface* mSurface = nullptr;
    double mPointerX = 0.0;
    double mPointerY = 0.0;
    bool mPointerDown = false;
    int mDownInputChannel = 0;
    bool mNeedReleaseConfirm = false;
    uint32_t mKeyboardMeta = 0;
#ifdef DROIDLET_HAVE_XKBCOMMON
    xkb_context* mXkbContext = nullptr;
    xkb_keymap* mXkbKeymap = nullptr;
    xkb_state* mXkbState = nullptr;
#endif
    int32_t mTouchId = -1;
    double mTouchX = 0.0;
    double mTouchY = 0.0;
    xdg_surface* mXdgSurface = nullptr;
    xdg_toplevel* mToplevel = nullptr;
    zxdg_decoration_manager_v1* mDecorationManager = nullptr;
    zxdg_toplevel_decoration_v1* mToplevelDecoration = nullptr;
    wl_shm_pool* mPool = nullptr;
    void* mPoolData = nullptr;
    size_t mPoolSize = 0;
    std::vector<std::unique_ptr<ShmBuffer>> mBuffers;
    std::vector<std::unique_ptr<RetiredPool>> mRetiredPools;
    int mPendingWidth = 0;
    int mPendingHeight = 0;
    bool mInteractiveResize = false;
    std::chrono::steady_clock::time_point mLastResize =
            std::chrono::steady_clock::time_point::min();

    wl_surface* mOverlaySurface = nullptr;
    wl_subsurface* mOverlaySubsurface = nullptr;
    wl_shm_pool* mOverlayPool = nullptr;
    wl_buffer* mOverlayBuffers[kOverlayBufferCount] = {};
    void* mOverlayData[kOverlayBufferCount] = {};
    void* mOverlayMapping = nullptr;
    size_t mOverlayPoolSize = 0;
    bool mOverlayBusy[kOverlayBufferCount] = {};

    wl_surface* mEffectSurface = nullptr;
    wl_subsurface* mEffectSubsurface = nullptr;
    int mEffectX = 0;
    int mEffectY = 0;
    int mEffectWidth = 0;
    int mEffectHeight = 0;
    wl_shm_pool* mEffectPool = nullptr;
    wl_buffer* mEffectBuffers[kEffectBufferCount] = {};
    void* mEffectData[kEffectBufferCount] = {};
    void* mEffectMapping = nullptr;
    size_t mEffectPoolSize = 0;
    bool mEffectBusy[kEffectBufferCount] = {};

    bool mConfigured = false;
};

}  // namespace

Presenter* createWaylandPresenter(int width, int height, const char* title) {
    auto* presenter = new WaylandPresenter(width, height, title);
    if (!presenter->open()) {
        delete presenter;
        return nullptr;
    }
    return presenter;
}

}  // namespace droidlet

#else  // !DROIDLET_HAVE_WAYLAND

namespace droidlet {
Presenter* createWaylandPresenter(int, int, const char*) { return nullptr; }
}  // namespace droidlet

#endif
