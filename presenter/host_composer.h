/*
 * In-process SurfaceFlinger stand-in for droidlet.
 *
 * droidlet has no binder driver, no surfaceflinger process and no gralloc HAL.
 * This class provides just enough of the SurfaceFlinger side of the graphics
 * stack for a real WindowManager/ViewRootImpl client surface:
 *
 *   - the legacy ISurfaceComposer service ("SurfaceFlinger") that receives
 *     Transaction::apply() calls,
 *   - the AIDL gui::ISurfaceComposer service ("SurfaceFlingerAIDL") that
 *     SurfaceComposerClient uses to create a connection and to query the
 *     BufferQueue acquired-buffer count,
 *   - the AIDL gui::ISurfaceComposerClient that creates layers.
 *
 * Buffers are real host gralloc buffers (see ui/GrallocHost.h).  The composer
 * latches them, draws the visible layers into a display-sized staging frame
 * and hands that frame to the host presenter (Wayland/X11/null).
 *
 * startHostComposer() must run before the Java runtime starts, so that the
 * services are registered before the first SurfaceComposerClient is created.
 */

#pragma once

#include <cstdint>

namespace droidlet {

// Registers the composer services and starts the presentation worker.
// width/height are the logical display size.  backend is passed straight to
// createPresenter() ("auto", "wayland", "x11" or "null").
// Returns false if no presenter could be created.
bool startHostComposer(int width, int height, const char* backend);

// Stops the worker, releases the presenter and unregisters the services.
void stopHostComposer();

// Asks the Java launcher to close the current task (used by the Wayland
// decoration close button).  Safe to call from any thread.
void requestHostClose();

// Resize the logical display after the compositor resized the host window
// (Wayland xdg_toplevel configure).  Resizes the composer/presenter surfaces
// and tells the Java launcher to update DisplayInfo and relayout activities.
// Safe to call from the presenter thread.
bool hostResizeDisplay(int width, int height);

// Frame access for the Java screenshot path.  The composer publishes a frame
// after each successful present(); these functions are safe to call from any
// thread and never block on the presenter itself.
bool waitHostFrame(int timeoutMs);
int copyHostFrame(uint32_t* pixels, int capacity);
int hostFrameWidth();
int hostFrameHeight();

// Presentation counters.  Any pointer may be null.
void hostFrameStats(uint64_t* latched, uint64_t* presented, uint64_t* dropped);
void hostRemoveLayer(int layerId);
bool hostLayerPosition(int layerId, float* x, float* y);

}  // namespace droidlet
