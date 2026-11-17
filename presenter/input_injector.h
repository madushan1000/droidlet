/*
 * Input injection into the app's window InputChannel.
 *
 * droidlet has no input dispatcher process.  The presenter and the optional
 * input script are the "input system": they synthesise Android input messages
 * and write them into the server end of the InputChannel that the launcher
 * handed to ViewRootImpl.
 *
 * All coordinates are display pixels.  eventTimeMs is a host monotonic
 * timestamp in milliseconds (Wayland/X11 timestamps); 0 means "now".
 */

#pragma once

#include <cstdint>
#include <jni.h>

namespace droidlet {

// Creates the InputChannel pair for the app window, keeps the server end for
// injection, and returns a Java android.view.InputChannel wrapping the client
// end.  Returns nullptr if a channel already exists or creation failed.
jobject createClientInputChannel(JNIEnv* env);

// True once at least one input channel exists.
bool inputReady();

// The id of the most recently created client channel, and activation of a
// previously created channel.  Host events go to the active channel.
int lastInputChannelId();
int activeInputChannelId();
void setActiveInputChannel(int id);

// Offset applied to injected pointer coordinates.  The app receives
// window-local coordinates, while the test scripts use display pixels.
void setInputWindowOffset(float x, float y);

// Extra offset to add to the composer layer position.  Popup windows have
// surface insets (for their shadow) that are not part of the layer position.
void setInputWindowInset(float x, float y);

// Composer layer id for the active window.  Pointer events query its current
// position so taps land correctly even while a popup is animating.
void setActiveInputLayer(int layerId);

void injectTouchDown(float x, float y, int64_t eventTimeMs, uint32_t metaState = 0);
void injectTouchMove(float x, float y, int64_t eventTimeMs, uint32_t metaState = 0);
void injectTouchUp(float x, float y, int64_t eventTimeMs, uint32_t metaState = 0);
void injectTouchCancel(float x, float y, int64_t eventTimeMs, uint32_t metaState = 0);
void injectHoverMove(float x, float y, int64_t eventTimeMs, uint32_t metaState = 0);
void injectScroll(float x, float y, float hscroll, float vscroll, int64_t eventTimeMs,
                  uint32_t metaState = 0);
void injectKey(int action, int keyCode, int scanCode, uint32_t metaState, int64_t eventTimeMs);

// Sends the dispatcher-level focus/touch-mode signals.  ViewRootImpl receives
// these through InputEventReceiver; there is no IWindow.windowFocusChanged()
// method in the modern window interface.
void injectFocus(bool hasFocus);
void injectTouchMode(bool inTouchMode);

// Drains finished/timeline responses the app sends back to the publisher.
// Call this periodically from the thread that drives the presenter.
void pumpInputResponses();

// Optional at-most-once input script.  Reads DROIDLET_INPUT_SCRIPT (a file) or
// DROIDLET_INPUT_EVENTS (inline ';'-separated commands).  Commands:
//   <time_ms> down|move|up|cancel|hover <x> <y>
//   <time_ms> scroll <x> <y> <vscroll> [hscroll]
//   <time_ms> key down|up <android-keycode> [meta]
// Time zero starts when the input channel becomes ready.
void pumpInputScript();

// True once a configured input script has run to completion.  False when no
// script was configured or the script is still pending.
bool inputScriptFinished();

}  // namespace droidlet
