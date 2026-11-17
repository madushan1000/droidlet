/*
 * Shared Wayland display context for the presenter and the EGL renderers.
 *
 * Wayland subsurfaces must belong to the same client connection as their
 * parent surface.  The presenter creates the host toplevel; the hwui/native
 * EGL bridges create app surfaces.  Both use this context so those surfaces
 * can be attached as subsurfaces of the presenter's window.
 */

#pragma once

#include <mutex>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_subcompositor;
struct wl_shm;
struct wl_surface;
struct wl_subsurface;
struct ANativeWindow;

namespace droidlet {

// Connects to the host compositor and binds the shared globals.  Idempotent.
bool waylandHostInit();

// Serializes every Wayland request from the presenter and from Mesa's EGL WSI.
// Wayland clients are not thread-safe; this is a process-wide lock.
std::recursive_mutex& waylandHostMutex();

wl_display* waylandHostDisplay();
wl_registry* waylandHostRegistry();
wl_compositor* waylandHostCompositor();
wl_subcompositor* waylandHostSubcompositor();
wl_shm* waylandHostShm();

// The presenter registers its xdg_toplevel surface here after creating it.
void waylandHostSetParentSurface(wl_surface* surface, int width, int height);
wl_surface* waylandHostParentSurface();
int waylandHostWidth();
int waylandHostHeight();

// Subsurface position is double-buffered and applied on the parent surface's
// commit.  After moving a shared child, commit the parent so the new position
// takes effect even when the main window is not drawing.
void waylandHostCommitParent();

// The presenter may create a dedicated overlay subsurface (performance
// overlay).  App subsurfaces register here so the overlay can be kept above
// them regardless of creation order.
void waylandHostSetOverlaySubsurface(wl_subsurface* subsurface, wl_surface* surface);
void waylandHostSetEffectSubsurface(wl_subsurface* subsurface, wl_surface* surface, int z);
void waylandHostSetEffectZ(int z);
void waylandHostRegisterChild(wl_subsurface* subsurface, wl_surface* childSurface, int z);
void waylandHostRaiseOverlay();
void waylandHostUpdateChildZ(wl_subsurface* subsurface, wl_surface* childSurface, int z);
void waylandHostUnregisterChild(wl_subsurface* subsurface);

// The presenter installs a callback that maps an ANativeWindow to its current
// Android layer position/geometry.  The EGL bridges use it to place shared
// subsurfaces (dialogs and popups) at the same location as the Android layer.
using LayerInfoCallback = bool (*)(ANativeWindow*, int*, int*, int*, int*, int*, float*, bool*);
void waylandHostSetLayerInfoCallback(LayerInfoCallback callback);
bool waylandHostLayerInfo(ANativeWindow* window, int* x, int* y, int* width, int* height,
                          int* z, float* alpha, bool* shareable);

using LayerSharedCallback = void (*)(ANativeWindow*, bool);
void waylandHostSetLayerSharedCallback(LayerSharedCallback callback);
void waylandHostSetLayerShared(ANativeWindow* window, bool shared);

// Monotonic count of zero-copy app buffer swaps, for the performance overlay.
void waylandHostRecordSwap();
uint64_t waylandHostSwapCount();

}  // namespace droidlet
