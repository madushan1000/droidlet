/*
 * Host EGL window-surface bridge.
 *
 * Android's EGL expects an ANativeWindow.  Desktop EGL expects a Wayland
 * wl_egl_window, an X11 window, or a GBM surface.  For the host build we use
 * the Wayland EGL platform: each Android Surface gets a small offscreen
 * wl_surface/wl_egl_window, the app renders into it, and eglSwapBuffers()
 * copies the result into the Android window buffer before presenting.
 *
 * If no Wayland display is available, callers fall back to the regular EGL
 * entry points.
 */

#pragma once

#include <EGL/egl.h>

struct ANativeWindow;

namespace android {

EGLDisplay hostEglGetDisplay(EGLNativeDisplayType nativeDisplay);
EGLBoolean hostEglTerminate(EGLDisplay display);
EGLSurface hostEglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                      ANativeWindow* window, const EGLint* attribs);
EGLBoolean hostEglDestroySurface(EGLDisplay display, EGLSurface surface);
EGLBoolean hostEglSwapBuffers(EGLDisplay display, EGLSurface surface);

}  // namespace android
