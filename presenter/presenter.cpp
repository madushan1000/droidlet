/*
 * Presenter factory and no-op backend.
 */

#include "presenter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace droidlet {
namespace {

class NullPresenter final : public Presenter {
public:
    bool present(const uint32_t*, int, int) override { return true; }
    bool resize(int width, int height) override {
        return width > 0 && height > 0;
    }
    void poll() override {}
    const char* name() const override { return "null"; }
};

bool envIsSet(const char* name) {
    const char* value = getenv(name);
    return value != nullptr && value[0] != '\0';
}

}  // namespace

Presenter* createNullPresenter() { return new NullPresenter(); }

Presenter* createPresenter(const char* backend, int width, int height,
                           const char* title) {
    const char* requested = (backend && backend[0]) ? backend : "auto";
    bool tryWayland = false;
    bool tryX11 = false;

    if (strcmp(requested, "auto") == 0) {
        // Prefer Wayland: the surface/commit/buffer model is the one we want
        // to keep using for a future real Surface implementation.
        if (envIsSet("WAYLAND_DISPLAY")) {
            tryWayland = true;
        } else if (envIsSet("DISPLAY")) {
            tryX11 = true;
        }
    } else if (strcmp(requested, "wayland") == 0) {
        tryWayland = true;
    } else if (strcmp(requested, "x11") == 0) {
        tryX11 = true;
    } else if (strcmp(requested, "null") == 0 || strcmp(requested, "none") == 0) {
        return createNullPresenter();
    } else {
        fprintf(stderr, "presenter: unknown backend \"%s\"\n", requested);
    }

    if (tryWayland) {
        Presenter* presenter = createWaylandPresenter(width, height, title);
        if (presenter) {
            return presenter;
        }
        if (strcmp(requested, "wayland") == 0) {
            return nullptr;
        }
        // Fall through to X11/none for "auto".
        tryX11 = envIsSet("DISPLAY");
    }
    if (tryX11) {
        Presenter* presenter = createX11Presenter(width, height, title);
        if (presenter) {
            return presenter;
        }
        if (strcmp(requested, "x11") == 0) {
            return nullptr;
        }
    }

    return createNullPresenter();
}

}  // namespace droidlet
