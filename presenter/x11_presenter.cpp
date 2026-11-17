/*
 * Optional X11 presenter.  This is deliberately a mirror of the Wayland
 * backend so the rest of droidlet never has to care which display server is
 * available.  It is only compiled when libX11 headers are present.
 */

#include "presenter.h"

#ifdef DROIDLET_HAVE_X11

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <android/input.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "host_composer.h"
#include "input_injector.h"
#include "input_keys.h"

namespace droidlet {
namespace {

class X11Presenter final : public Presenter {
public:
    X11Presenter(int width, int height, const char* title)
        : mWidth(width), mHeight(height), mTitle(title ? title : "droidlet") {}

    ~X11Presenter() override { close(); }

    bool open() {
        mDisplay = XOpenDisplay(nullptr);
        if (!mDisplay) {
            fprintf(stderr, "presenter: x11: XOpenDisplay failed\n");
            return false;
        }
        int screen = DefaultScreen(mDisplay);
        mWindow = XCreateSimpleWindow(mDisplay, RootWindow(mDisplay, screen), 0, 0,
                                      mWidth, mHeight, 0,
                                      BlackPixel(mDisplay, screen),
                                      WhitePixel(mDisplay, screen));
        XStoreName(mDisplay, mWindow, mTitle.c_str());
        XSelectInput(mDisplay, mWindow,
                     ExposureMask | StructureNotifyMask | PointerMotionMask |
                             ButtonPressMask | ButtonReleaseMask | KeyPressMask |
                             KeyReleaseMask | EnterWindowMask | LeaveWindowMask |
                             FocusChangeMask);
        mWmDelete = XInternAtom(mDisplay, "WM_DELETE_WINDOW", False);
        if (mWmDelete != None) {
            XSetWMProtocols(mDisplay, mWindow, &mWmDelete, 1);
        }
        XMapWindow(mDisplay, mWindow);
        // Give the host window keyboard/pointer focus even when there is no
        // window manager to do it for us (for example under Xvfb).
        XSetInputFocus(mDisplay, mWindow, RevertToParent, CurrentTime);
        XFlush(mDisplay);

        mImage = XCreateImage(mDisplay, DefaultVisual(mDisplay, screen),
                              DefaultDepth(mDisplay, screen), ZPixmap, 0, nullptr,
                              mWidth, mHeight, 32, 0);
        if (!mImage) {
            fprintf(stderr, "presenter: x11: XCreateImage failed\n");
            close();
            return false;
        }
        mImage->data = static_cast<char*>(calloc(mImage->bytes_per_line * mHeight, 1));
        if (!mImage->data) {
            close();
            return false;
        }
        mGc = XCreateGC(mDisplay, mWindow, 0, nullptr);
        fprintf(stderr, "presenter: x11: window %dx%d title=\"%s\"\n",
                mWidth, mHeight, mTitle.c_str());
        return true;
    }

    bool present(const uint32_t* pixels, int width, int height) override {
        if (!mDisplay || !mImage || width != mWidth || height != mHeight) {
            return false;
        }
        // XImage rows are bytes_per_line apart; Android bitmaps are tightly
        // packed, so copy row by row.
        auto* dst = reinterpret_cast<uint8_t*>(mImage->data);
        for (int y = 0; y < height; ++y) {
            memcpy(dst + y * mImage->bytes_per_line,
                   pixels + static_cast<size_t>(y) * width,
                   static_cast<size_t>(width) * sizeof(uint32_t));
        }
        XPutImage(mDisplay, mWindow, mGc, mImage, 0, 0, 0, 0, width, height);
        XFlush(mDisplay);
        return true;
    }

    bool resize(int width, int height) override {
        if (!mDisplay || !mWindow || width <= 0 || height <= 0 ||
            (width == mWidth && height == mHeight)) {
            return width > 0 && height > 0;
        }
        int screen = DefaultScreen(mDisplay);
        if (mImage) {
            XDestroyImage(mImage);
            mImage = nullptr;
        }
        mWidth = width;
        mHeight = height;
        XResizeWindow(mDisplay, mWindow, mWidth, mHeight);
        mImage = XCreateImage(mDisplay, DefaultVisual(mDisplay, screen),
                              static_cast<unsigned int>(DefaultDepth(mDisplay, screen)),
                              ZPixmap, 0, nullptr, mWidth, mHeight, 32, 0);
        if (!mImage) {
            return false;
        }
        mImage->data = static_cast<char*>(calloc(mImage->bytes_per_line * mHeight, 1));
        if (!mImage->data) {
            XDestroyImage(mImage);
            mImage = nullptr;
            return false;
        }
        XFlush(mDisplay);
        return true;
    }

    void poll() override {
        if (!mDisplay) {
            return;
        }
        while (XPending(mDisplay) > 0) {
            XEvent event;
            XNextEvent(mDisplay, &event);
            handleEvent(event);
        }
        XFlush(mDisplay);
    }

    const char* name() const override { return "x11"; }

private:
    static uint32_t metaFromState(unsigned int state) {
        uint32_t meta = 0;
        if (state & ShiftMask) meta |= kMetaShiftOn;
        if (state & ControlMask) meta |= kMetaCtrlOn;
        if (state & Mod1Mask) meta |= kMetaAltOn;
        if (state & Mod4Mask) meta |= kMetaMetaOn;
        return meta;
    }

    void handleEvent(XEvent& event) {
        switch (event.type) {
            case MotionNotify:
                mPointerX = event.xmotion.x;
                mPointerY = event.xmotion.y;
                if (mPointerDown) {
                    injectTouchMove(mPointerX, mPointerY, event.xmotion.time,
                                    metaFromState(event.xmotion.state));
                } else {
                    if (mNeedReleaseConfirm) {
                        // Only send the confirmation to the window that saw
                        // the press.  A popup/dialog may have opened since
                        // then; a stray UP outside its bounds would cancel it.
                        if (activeInputChannelId() == mDownInputChannel) {
                            injectTouchUp(mPointerX, mPointerY, event.xmotion.time,
                                          metaFromState(event.xmotion.state));
                        }
                        mNeedReleaseConfirm = false;
                    }
                    injectHoverMove(mPointerX, mPointerY, event.xmotion.time,
                                    metaFromState(event.xmotion.state));
                }
                break;

            case ButtonPress:
                mPointerX = event.xbutton.x;
                mPointerY = event.xbutton.y;
                if (event.xbutton.button == Button1) {
                    mPointerDown = true;
                    mNeedReleaseConfirm = false;
                    mDownInputChannel = activeInputChannelId();
                    injectFocus(true);
                    // Keep receiving motion if the drag leaves the window.
                    XGrabPointer(mDisplay, mWindow, True,
                                 ButtonReleaseMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync, None, None,
                                 CurrentTime);
                    injectTouchDown(mPointerX, mPointerY, event.xbutton.time,
                                    metaFromState(event.xbutton.state));
                } else if (event.xbutton.button == Button4) {
                    injectScroll(mPointerX, mPointerY, 0.0f, 1.0f,
                                 event.xbutton.time, metaFromState(event.xbutton.state));
                } else if (event.xbutton.button == Button5) {
                    injectScroll(mPointerX, mPointerY, 0.0f, -1.0f,
                                 event.xbutton.time, metaFromState(event.xbutton.state));
                } else if (event.xbutton.button == 6) {
                    injectScroll(mPointerX, mPointerY, -1.0f, 0.0f,
                                 event.xbutton.time, metaFromState(event.xbutton.state));
                } else if (event.xbutton.button == 7) {
                    injectScroll(mPointerX, mPointerY, 1.0f, 0.0f,
                                 event.xbutton.time, metaFromState(event.xbutton.state));
                }
                break;

            case ButtonRelease:
                if (event.xbutton.button == Button1) {
                    mPointerX = event.xbutton.x;
                    mPointerY = event.xbutton.y;
                    injectTouchUp(mPointerX, mPointerY, event.xbutton.time,
                                  metaFromState(event.xbutton.state));
                    mPointerDown = false;
                    mNeedReleaseConfirm = true;
                    XUngrabPointer(mDisplay, CurrentTime);
                }
                break;

            case LeaveNotify:
                if (mPointerDown) {
                    // End the drag rather than cancelling: NativeActivity games
                    // may treat CANCEL as a move and remain stuck at the last
                    // steering target.
                    injectTouchUp(mPointerX, mPointerY, event.xcrossing.time,
                                  metaFromState(event.xcrossing.state));
                    mPointerDown = false;
                    mNeedReleaseConfirm = true;
                    XUngrabPointer(mDisplay, CurrentTime);
                }
                break;

            case FocusIn:
                injectFocus(true);
                break;

            case ClientMessage:
                if (mWmDelete != None &&
                        static_cast<Atom>(event.xclient.data.l[0]) == mWmDelete) {
                    requestHostClose();
                }
                break;

            case KeyPress:
            case KeyRelease: {
                uint32_t extraMeta = 0;
                const KeySym keysym = XLookupKeysym(&event.xkey, 0);
                const int keyCode = androidKeyCodeFromKeysym(
                        static_cast<uint32_t>(keysym), &extraMeta);
                if (keyCode == 0) {
                    break;
                }
                const int action = event.type == KeyPress ? AKEY_EVENT_ACTION_DOWN
                                                          : AKEY_EVENT_ACTION_UP;
                injectKey(action, keyCode, static_cast<int>(event.xkey.keycode),
                          metaFromState(event.xkey.state) | extraMeta, event.xkey.time);
                break;
            }

            default:
                break;
        }
    }

    void close() {
        if (mGc && mDisplay) {
            XFreeGC(mDisplay, mGc);
            mGc = nullptr;
        }
        if (mImage) {
            // XDestroyImage frees mImage->data.
            XDestroyImage(mImage);
            mImage = nullptr;
        }
        if (mWindow && mDisplay) {
            XDestroyWindow(mDisplay, mWindow);
            mWindow = 0;
        }
        if (mDisplay) {
            XCloseDisplay(mDisplay);
            mDisplay = nullptr;
        }
    }

    int mWidth;
    int mHeight;
    std::string mTitle;
    Display* mDisplay = nullptr;
    Window mWindow = 0;
    Atom mWmDelete = None;
    GC mGc = nullptr;
    XImage* mImage = nullptr;
    int mPointerX = 0;
    int mPointerY = 0;
    bool mPointerDown = false;
    int mDownInputChannel = 0;
    bool mNeedReleaseConfirm = false;
};

}  // namespace

Presenter* createX11Presenter(int width, int height, const char* title) {
    auto* presenter = new X11Presenter(width, height, title);
    if (!presenter->open()) {
        delete presenter;
        return nullptr;
    }
    return presenter;
}

}  // namespace droidlet

#else  // !DROIDLET_HAVE_X11

namespace droidlet {
Presenter* createX11Presenter(int, int, const char*) { return nullptr; }
}  // namespace droidlet

#endif
