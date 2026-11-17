/*
 * Host EGL window-surface bridge.
 *
 * Android EGL creates window surfaces from ANativeWindow.  Desktop Mesa
 * expects a Wayland wl_egl_window (or X11/GBM) instead.  For the host build we
 * create a small offscreen Wayland surface for each Android window and copy
 * the rendered frame into the Android window buffer in eglSwapBuffers().
 */

#define LOG_TAG "droidlet-egl"

#include "host_egl_window.h"
#include "wayland_host.h"

#include <android/native_window.h>
#include <android/rect.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <log/log.h>


namespace android {
namespace {

constexpr int kWindowFormatBgra8888 = 5;  // AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
constexpr int kWindowFormatRgb565 = 4;    // AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM

struct ManagedSurface {
    ANativeWindow* window = nullptr;
    wl_surface* waylandSurface = nullptr;
    wl_egl_window* eglWindow = nullptr;
    int width = 0;
    int height = 0;
    bool pbuffer = false;
    wl_subsurface* subsurface = nullptr;
    bool sharedSubsurface = false;
};

// Wayland compositors do not reliably send frame callbacks for the offscreen
// surfaces used by the bridge, so eglSwapBuffers can block indefinitely after
// a few frames.  Render to a pbuffer and pace presentation locally instead.

std::mutex gPaceMutex;
std::chrono::steady_clock::time_point gNextPresent;

void pacePresentation() {
    constexpr auto kFrameInterval = std::chrono::microseconds(16667);
    std::lock_guard lock(gPaceMutex);
    const auto now = std::chrono::steady_clock::now();
    if (gNextPresent > now) {
        std::this_thread::sleep_for(gNextPresent - now);
    }
    const auto after = std::chrono::steady_clock::now();
    gNextPresent = std::max(after, gNextPresent) + kFrameInterval;
}

void logSharedSwap() {
    if (std::getenv("DROIDLET_EGL_SUBSURFACE_STATS") == nullptr) {
        return;
    }
    static std::atomic<uint64_t> count{0};
    static std::mutex statsMutex;
    static auto start = std::chrono::steady_clock::now();
    const uint64_t frames = ++count;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    if (elapsed.count() >= 1000) {
        ALOGI("shared EGL swaps: %.1f fps", frames * 1000.0 / elapsed.count());
        count = 0;
        start = now;
    }
}

struct HostEglState {
    std::mutex mutex;
    wl_display* waylandDisplay = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    bool waylandEgl = false;
    bool sharedSubsurface = false;
    bool initialized = false;
    bool failed = false;
    bool tried = false;
    std::unordered_map<EGLSurface, ManagedSurface> surfaces;
};

HostEglState gState;

void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                    uint32_t version) {
    (void)data;
    (void)version;
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        gState.compositor = static_cast<wl_compositor*>(
                wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    }
}

void registryGlobalRemove(void* data, wl_registry* registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

const wl_registry_listener kRegistryListener = {registryGlobal, registryGlobalRemove};

bool sharedSubsurfaceRequested() {
    const char* value = std::getenv("DROIDLET_EGL_SUBSURFACE");
    return value != nullptr && value[0] != '\0' &&
            std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

bool ensureLocked() {
    if (gState.initialized) {
        return true;
    }
    if (gState.failed) {
        return false;
    }
    if (gState.tried && gState.eglDisplay == EGL_NO_DISPLAY) {
        return false;
    }
    gState.tried = true;

    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    const char* displayName = std::getenv("DISPLAY");
    const char* waylandName = std::getenv("WAYLAND_DISPLAY");

    if (sharedSubsurfaceRequested() &&
            droidlet::waylandHostInit() &&
            droidlet::waylandHostParentSurface() != nullptr &&
            droidlet::waylandHostCompositor() != nullptr &&
            droidlet::waylandHostSubcompositor() != nullptr &&
            getPlatformDisplay != nullptr) {
        gState.eglDisplay = getPlatformDisplay(
                EGL_PLATFORM_WAYLAND_KHR, droidlet::waylandHostDisplay(), nullptr);
        if (gState.eglDisplay != EGL_NO_DISPLAY) {
            gState.waylandEgl = true;
            gState.sharedSubsurface = true;
            gState.initialized = true;
            ALOGI("using shared Wayland subsurfaces");
            return true;
        }
    }
    const char* platform = std::getenv("DROIDLET_EGL_PLATFORM");
    const bool forceWayland = platform != nullptr && std::strcmp(platform, "wayland") == 0;
    const bool forceX11 = platform != nullptr && std::strcmp(platform, "x11") == 0;
    const bool tryWaylandFirst = forceWayland || (!forceX11 && displayName == nullptr);

    // Prefer X11 when available: its EGL pbuffer path uses the host GPU more
    // reliably than Mesa's Wayland WSI in headless/nested compositors.
    if (getPlatformDisplay != nullptr && displayName != nullptr && !tryWaylandFirst) {
        gState.eglDisplay = getPlatformDisplay(EGL_PLATFORM_X11_KHR, EGL_DEFAULT_DISPLAY, nullptr);
    }

    if (gState.eglDisplay == EGL_NO_DISPLAY && waylandName != nullptr && tryWaylandFirst) {
        if (gState.waylandDisplay == nullptr) {
            gState.waylandDisplay = wl_display_connect(nullptr);
        }
        if (gState.waylandDisplay != nullptr) {
            gState.registry = wl_display_get_registry(gState.waylandDisplay);
            wl_registry_add_listener(gState.registry, &kRegistryListener, nullptr);
            if (wl_display_roundtrip(gState.waylandDisplay) >= 0 &&
                    gState.compositor != nullptr) {
                if (getPlatformDisplay != nullptr) {
                    gState.eglDisplay = getPlatformDisplay(
                            EGL_PLATFORM_WAYLAND_KHR, gState.waylandDisplay, nullptr);
                }
                if (gState.eglDisplay == EGL_NO_DISPLAY) {
                    gState.eglDisplay = eglGetDisplay(
                            reinterpret_cast<EGLNativeDisplayType>(gState.waylandDisplay));
                }
                gState.waylandEgl = gState.eglDisplay != EGL_NO_DISPLAY;
            }
        }
    }

    if (gState.eglDisplay == EGL_NO_DISPLAY) {
        gState.failed = true;
        return false;
    }
    gState.initialized = true;
    return true;
}

void releaseManagedLocked(ManagedSurface& managed, bool destroyEglSurface, EGLDisplay display,
                          EGLSurface surface) {
    if (destroyEglSurface && display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE) {
        eglDestroySurface(display, surface);
    }
    if (managed.subsurface != nullptr) {
        droidlet::waylandHostUnregisterChild(managed.subsurface);
        wl_subsurface_destroy(managed.subsurface);
    }
    if (managed.eglWindow != nullptr) {
        wl_egl_window_destroy(managed.eglWindow);
    }
    if (managed.waylandSurface != nullptr) {
        wl_surface_destroy(managed.waylandSurface);
    }
    if (managed.window != nullptr) {
        ANativeWindow_release(managed.window);
    }
    managed = {};
}

void copyToWindow(const ManagedSurface& managed, const std::vector<uint8_t>& pixels) {
    ANativeWindow_Buffer buffer;
    ARect dirtyBounds = {0, 0, managed.width, managed.height};
    if (ANativeWindow_lock(managed.window, &buffer, &dirtyBounds) != 0) {
        return;
    }

    const int32_t copyWidth = std::min(managed.width, buffer.width);
    const int32_t copyHeight = std::min(managed.height, buffer.height);
    const bool bgra = buffer.format == kWindowFormatBgra8888;
    const bool rgb565 = buffer.format == kWindowFormatRgb565;
    const size_t dstStride = static_cast<size_t>(buffer.stride) * (rgb565 ? 2 : 4);
    const uint8_t* srcBase = pixels.data();
    uint8_t* dstBase = static_cast<uint8_t*>(buffer.bits);

    for (int32_t y = 0; y < copyHeight; ++y) {
        const uint8_t* src =
                srcBase + static_cast<size_t>(managed.height - 1 - y) * managed.width * 4;
        uint8_t* dst = dstBase + static_cast<size_t>(y) * dstStride;
        if (rgb565) {
            uint16_t* dst565 = reinterpret_cast<uint16_t*>(dst);
            for (int32_t x = 0; x < copyWidth; ++x) {
                const uint8_t r = src[x * 4 + 0];
                const uint8_t g = src[x * 4 + 1];
                const uint8_t b = src[x * 4 + 2];
                dst565[x] = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        } else if (bgra) {
            for (int32_t x = 0; x < copyWidth; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        } else {
            std::memcpy(dst, src, static_cast<size_t>(copyWidth) * 4);
        }
    }

    ANativeWindow_unlockAndPost(managed.window);
}

}  // namespace

EGLDisplay hostEglGetDisplay(EGLNativeDisplayType) {
    std::lock_guard lock(gState.mutex);
    if (ensureLocked()) {
        return gState.eglDisplay;
    }
    return EGL_NO_DISPLAY;
}

EGLBoolean hostEglTerminate(EGLDisplay display) {
    std::lock_guard lock(gState.mutex);
    if (display != gState.eglDisplay) {
        return eglTerminate(display);
    }
    for (auto& [surface, managed] : gState.surfaces) {
        releaseManagedLocked(managed, false, EGL_NO_DISPLAY, surface);
    }
    gState.surfaces.clear();
    // Do not terminate the shared host EGLDisplay.  hwui's RenderThread and
    // the presenter use the same Mesa display object, and a NativeActivity's
    // eglTerminate() during teardown would otherwise invalidate their context
    // (EGL_BAD_DISPLAY, followed by an hwui abort).
    gState.initialized = false;
    gState.tried = false;
    return EGL_TRUE;
}

EGLSurface hostEglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                      ANativeWindow* window, const EGLint* attribs) {
    std::lock_guard lock(gState.mutex);
    if (!ensureLocked() || display != gState.eglDisplay || window == nullptr) {
        // Passing an Android ANativeWindow to desktop EGL is invalid.  No
        // window surface is available without the Wayland bridge.
        return EGL_NO_SURFACE;
    }

    const int width = std::max(1, ANativeWindow_getWidth(window));
    const int height = std::max(1, ANativeWindow_getHeight(window));

    // On a Wayland EGL display, render into a normal window surface backed by
    // an unmapped wl_surface.  It has no xdg_toplevel role, so the compositor
    // never displays it; it is only an offscreen render target.  This replaces
    // the per-window pbuffer with the WSI path Mesa is tuned for.
    if (gState.sharedSubsurface) {
        int layerX = 0;
        int layerY = 0;
        int layerZ = 0;
        bool shareable = true;
        droidlet::waylandHostLayerInfo(window, &layerX, &layerY, nullptr, nullptr, &layerZ,
                                       nullptr, &shareable);
        if (shareable) {
            std::lock_guard<std::recursive_mutex> lock(droidlet::waylandHostMutex());
            wl_surface* waylandSurface =
                    wl_compositor_create_surface(droidlet::waylandHostCompositor());
            if (waylandSurface != nullptr) {
                wl_egl_window* eglWindow = wl_egl_window_create(waylandSurface, width, height);
                if (eglWindow != nullptr) {
                    EGLSurface surface = eglCreateWindowSurface(
                            gState.eglDisplay, config,
                            reinterpret_cast<EGLNativeWindowType>(eglWindow), attribs);
                    if (surface != EGL_NO_SURFACE) {
                        wl_subsurface* subsurface = wl_subcompositor_get_subsurface(
                                droidlet::waylandHostSubcompositor(), waylandSurface,
                                droidlet::waylandHostParentSurface());
                        if (subsurface != nullptr) {
                            if (std::getenv("DROIDLET_COMPOSER_DEBUG") != nullptr) {
                                fprintf(stderr, "native-egl: shared child %dx%d at %d,%d z=%d\n",
                                        width, height, layerX, layerY, layerZ);
                            }
                            wl_subsurface_set_position(subsurface, layerX, layerY);
                            wl_subsurface_set_desync(subsurface);
                            droidlet::waylandHostRegisterChild(
                                    subsurface, waylandSurface, layerZ);
                            wl_region* empty = wl_compositor_create_region(
                                    droidlet::waylandHostCompositor());
                            if (empty != nullptr) {
                                wl_surface_set_input_region(waylandSurface, empty);
                                wl_region_destroy(empty);
                            }
                            // App buffers often carry alpha=0 in untouched
                            // pixels (GL clears with alpha 0).  The layer is
                            // opaque, so tell the compositor to ignore alpha.
                            wl_region* opaque = wl_compositor_create_region(
                                    droidlet::waylandHostCompositor());
                            if (opaque != nullptr) {
                                wl_region_add(opaque, 0, 0, width, height);
                                wl_surface_set_opaque_region(waylandSurface, opaque);
                                wl_region_destroy(opaque);
                            }
                            wl_surface_commit(waylandSurface);
                            ANativeWindow_acquire(window);
                            gState.surfaces[surface] = ManagedSurface{
                                    window, waylandSurface, eglWindow, width, height, false,
                                    subsurface, true};
                            droidlet::waylandHostSetLayerShared(window, true);
                            return surface;
                        }
                    }
                    wl_egl_window_destroy(eglWindow);
                }
                wl_surface_destroy(waylandSurface);
            }
            ALOGW("failed to create shared Wayland subsurface; falling back");
        }
        // If the layer state is not representable as a subsurface, fall
        // through to the unmapped window surface + readback path.  The host
        // composer will draw that layer into the effect overlay.
        droidlet::waylandHostSetLayerShared(window, false);
    }

    const bool forcePbuffer = std::getenv("DROIDLET_EGL_PBUFFER") != nullptr;
    wl_compositor* compositor = gState.compositor != nullptr
            ? gState.compositor : droidlet::waylandHostCompositor();
    if (!forcePbuffer && gState.waylandEgl && compositor != nullptr) {
        wl_surface* waylandSurface = wl_compositor_create_surface(compositor);
        if (waylandSurface != nullptr) {
            wl_egl_window* eglWindow = wl_egl_window_create(waylandSurface, width, height);
            if (eglWindow != nullptr) {
                EGLSurface surface = eglCreateWindowSurface(
                        gState.eglDisplay, config,
                        reinterpret_cast<EGLNativeWindowType>(eglWindow), attribs);
                if (surface != EGL_NO_SURFACE) {
                    static bool logged = false;
                    if (!logged) {
                        logged = true;
                        ALOGI("using unmapped Wayland window surfaces");
                    }
                    ANativeWindow_acquire(window);
                    gState.surfaces[surface] = ManagedSurface{
                            window, waylandSurface, eglWindow, width, height, false};
                    return surface;
                }
                wl_egl_window_destroy(eglWindow);
            }
            wl_surface_destroy(waylandSurface);
        }
        // Fall through to a pbuffer if the config cannot make a window surface.
    }

    const EGLint pbufferAttribs[] = {
            EGL_WIDTH, width,
            EGL_HEIGHT, height,
            EGL_NONE,
    };
    EGLSurface surface = eglCreatePbufferSurface(gState.eglDisplay, config, pbufferAttribs);
    if (surface != EGL_NO_SURFACE) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            ALOGI("using pbuffer surfaces");
        }
        ANativeWindow_acquire(window);
        gState.surfaces[surface] = ManagedSurface{window, nullptr, nullptr, width, height, true};
        return surface;
    }
    return EGL_NO_SURFACE;
}

EGLBoolean hostEglDestroySurface(EGLDisplay display, EGLSurface surface) {
    std::lock_guard lock(gState.mutex);
    auto it = gState.surfaces.find(surface);
    if (it != gState.surfaces.end()) {
        ManagedSurface managed = it->second;
        gState.surfaces.erase(it);
        releaseManagedLocked(managed, true, display, surface);
        return EGL_TRUE;
    }
    return eglDestroySurface(display, surface);
}

EGLBoolean hostEglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    ManagedSurface managed;
    {
        std::lock_guard lock(gState.mutex);
        auto it = gState.surfaces.find(surface);
        if (it == gState.surfaces.end()) {
            return eglSwapBuffers(display, surface);
        }
        managed = it->second;
    }

    if (managed.sharedSubsurface) {
        EGLBoolean result;
        {
            std::lock_guard<std::recursive_mutex> lock(droidlet::waylandHostMutex());
            if (managed.subsurface != nullptr && managed.window != nullptr) {
                int layerX = 0;
                int layerY = 0;
                int layerZ = 0;
                if (droidlet::waylandHostLayerInfo(managed.window, &layerX, &layerY, nullptr,
                                                   nullptr, &layerZ, nullptr, nullptr)) {
                    wl_subsurface_set_position(managed.subsurface, layerX, layerY);
                    droidlet::waylandHostUpdateChildZ(
                            managed.subsurface, managed.waylandSurface, layerZ);
                }
            }
            // Do not wait for a frame callback while holding the shared
            // Wayland lock: the presenter thread needs it to dispatch events.
            eglSwapInterval(display, 0);
            result = eglSwapBuffers(display, surface);
            wl_display_flush(droidlet::waylandHostDisplay());
        }
        droidlet::waylandHostRecordSwap();
        logSharedSwap();
        // The app's swap is unthrottled with interval 0; cap it at the host's
        // 60 Hz presentation cadence without holding the Wayland lock.
        pacePresentation();
        return result;
    }

    // Both paths copy the rendered frame; pace presentation locally so an
    // unmapped window surface with a zero swap interval does not busy-loop.
    pacePresentation();

    std::vector<uint8_t> pixels(
            static_cast<size_t>(managed.width) * static_cast<size_t>(managed.height) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    while (glGetError() != GL_NO_ERROR) {
    }
    glReadPixels(0, 0, managed.width, managed.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() == GL_NO_ERROR) {
        copyToWindow(managed, pixels);
    }

    if (managed.pbuffer) {
        // A pbuffer has no back buffer to swap; the frame was already copied.
        return EGL_TRUE;
    }
    // The surface is deliberately unmapped.  Do not wait for a compositor
    // frame callback that will never arrive; present with a zero swap interval.
    eglSwapInterval(display, 0);
    return eglSwapBuffers(display, surface);
}

}  // namespace android

// bionic_translation resolves the `bionic_` prefix first when it relocates
// application libraries.  These hooks route native Android EGL calls through
// the same host bridge used by the framework EGL JNI, so NDK apps can create a
// window surface from an ANativeWindow and have it presented to the host.

extern "C" {

__attribute__((visibility("default")))
EGLDisplay bionic_eglGetDisplay(EGLNativeDisplayType nativeDisplay) {
    return android::hostEglGetDisplay(nativeDisplay);
}

__attribute__((visibility("default")))
EGLSurface bionic_eglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                         EGLNativeWindowType window, const EGLint* attribs) {
    return android::hostEglCreateWindowSurface(
            display, config, reinterpret_cast<ANativeWindow*>(window), attribs);
}

__attribute__((visibility("default")))
EGLBoolean bionic_eglDestroySurface(EGLDisplay display, EGLSurface surface) {
    return android::hostEglDestroySurface(display, surface);
}

__attribute__((visibility("default")))
EGLBoolean bionic_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    return android::hostEglSwapBuffers(display, surface);
}

__attribute__((visibility("default")))
EGLBoolean bionic_eglTerminate(EGLDisplay display) {
    return android::hostEglTerminate(display);
}

}  // extern "C"
