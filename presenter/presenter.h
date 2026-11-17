/*
 * Host-side presenter abstraction for droidlet.
 *
 * The in-process composer (host_composer.cpp) blits the real Android Surface
 * buffers into a display-sized frame and hands that frame to one of these
 * backends.  Backends are intentionally small: they only need "open a window",
 * "upload a frame" and "poll events".
 *
 * Wayland is the primary backend (its surface/commit/buffer model maps closely
 * onto Android's Surface/BufferQueue model).  X11 is optional and selected only
 * when it was compiled in and Wayland is unavailable.
 */

#pragma once

#include <cstdint>

namespace droidlet {

class Presenter {
public:
    virtual ~Presenter() = default;

    // Upload one ARGB_8888 frame.  The source uses 0xAARRGGBB in a uint32_t,
    // the same layout as android.graphics.Bitmap.getPixels().
    // Returns false when the backend has no free buffer right now.
    virtual bool present(const uint32_t* pixels, int width, int height) = 0;

    // Backends that can place the performance overlay in a separate
    // compositor surface should return true and implement presentOverlay().
    virtual bool supportsOverlay() const { return false; }
    virtual bool presentOverlay(const uint32_t*, int, int) { return false; }

    // A transparent CPU-composed layer stack placed between the main app
    // surface and shared dialogs.  Used for color-only/effect layers and for
    // app layers whose state a plain wl_subsurface cannot represent.
    // `pixels` is a frameWidth x frameHeight ARGB buffer; only the rect
    // (x, y, width, height) is uploaded, and the effect surface is placed at
    // that position in toplevel coordinates so the compositor does not have
    // to handle a full-screen transparent surface for a small video/dialog.
    virtual bool supportsEffect() const { return false; }
    virtual bool presentEffect(const uint32_t*, int, int, int, int, int, int, int) {
        return false;
    }

    // Resize the backend surface and its buffers after the compositor resized
    // the host window.  Backends that cannot resize return false; the
    // composer then keeps composing into the new logical size but the frames
    // will not be visible until the backend catches up.
    virtual bool resize(int width, int height) { (void)width; (void)height; return false; }

    // Process pending display-server events without blocking.
    virtual void poll() = 0;

    // Human-readable backend name ("wayland", "x11").
    virtual const char* name() const = 0;
};

// backend: "auto", "wayland", "x11" or "null".  "auto" prefers Wayland when
// WAYLAND_DISPLAY is set, then X11 when DISPLAY is set.
Presenter* createPresenter(const char* backend, int width, int height, const char* title);

Presenter* createWaylandPresenter(int width, int height, const char* title);
Presenter* createX11Presenter(int width, int height, const char* title);
Presenter* createNullPresenter();

}  // namespace droidlet
