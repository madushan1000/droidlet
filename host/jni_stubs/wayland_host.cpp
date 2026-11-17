#include "wayland_host.h"

#include <wayland-client.h>

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace droidlet {
namespace {

struct Host {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_subcompositor* subcompositor = nullptr;
    wl_shm* shm = nullptr;
    wl_surface* parent = nullptr;
    wl_subsurface* overlay = nullptr;
    wl_surface* overlaySurface = nullptr;
    wl_subsurface* effect = nullptr;
    wl_surface* effectSurface = nullptr;
    int effectZ = 0;
    int width = 0;
    int height = 0;
    bool initialized = false;
    bool failed = false;
};

Host gHost;
LayerInfoCallback gLayerInfoCallback = nullptr;
LayerSharedCallback gLayerSharedCallback = nullptr;

struct ChildSurface {
    wl_subsurface* subsurface = nullptr;
    wl_surface* surface = nullptr;
    int z = 0;
};
std::vector<ChildSurface> gChildren;
std::atomic<uint64_t> gSwapCount{0};

void reorderChildrenLocked() {
    struct Item {
        wl_subsurface* subsurface;
        wl_surface* surface;
        int z;
    };
    std::vector<Item> items;
    items.reserve(gChildren.size() + 1);
    for (const auto& child : gChildren) {
        items.push_back(Item{child.subsurface, child.surface, child.z});
    }
    if (gHost.effect != nullptr) {
        items.push_back(Item{gHost.effect, gHost.effectSurface, gHost.effectZ});
    }
    if (items.empty()) {
        return;
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.z < b.z; });
    if (items.size() > 1) {
        wl_subsurface_place_below(items[0].subsurface, items[1].surface);
        for (size_t i = 1; i < items.size(); ++i) {
            wl_subsurface_place_above(items[i].subsurface, items[i - 1].surface);
        }
    }
    if (gHost.overlay != nullptr) {
        wl_subsurface_place_above(gHost.overlay, items.back().surface);
        if (gHost.overlaySurface != nullptr) {
            wl_surface_commit(gHost.overlaySurface);
        }
    }
}

void registryGlobal(void*, wl_registry* registry, uint32_t name, const char* interface,
                    uint32_t version) {
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        gHost.compositor = static_cast<wl_compositor*>(wl_registry_bind(
                registry, name, &wl_compositor_interface, version < 4 ? version : 4));
    } else if (std::strcmp(interface, wl_subcompositor_interface.name) == 0) {
        gHost.subcompositor = static_cast<wl_subcompositor*>(
                wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        gHost.shm = static_cast<wl_shm*>(
                wl_registry_bind(registry, name, &wl_shm_interface, 1));
    }
}

void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {registryGlobal, registryGlobalRemove};

}  // namespace

bool waylandHostInit() {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    if (gHost.initialized) {
        return true;
    }
    if (gHost.failed) {
        return false;
    }
    if (std::getenv("WAYLAND_DISPLAY") == nullptr) {
        gHost.failed = true;
        return false;
    }
    gHost.display = wl_display_connect(nullptr);
    if (gHost.display == nullptr) {
        gHost.failed = true;
        return false;
    }
    gHost.registry = wl_display_get_registry(gHost.display);
    if (gHost.registry == nullptr) {
        gHost.failed = true;
        return false;
    }
    wl_registry_add_listener(gHost.registry, &kRegistryListener, nullptr);
    if (wl_display_roundtrip(gHost.display) < 0 || gHost.compositor == nullptr ||
            gHost.subcompositor == nullptr) {
        gHost.failed = true;
        return false;
    }
    gHost.initialized = true;
    return true;
}

std::recursive_mutex& waylandHostMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

wl_display* waylandHostDisplay() { return gHost.display; }
wl_registry* waylandHostRegistry() { return gHost.registry; }
wl_compositor* waylandHostCompositor() { return gHost.compositor; }
wl_subcompositor* waylandHostSubcompositor() { return gHost.subcompositor; }
wl_shm* waylandHostShm() { return gHost.shm; }

void waylandHostSetParentSurface(wl_surface* surface, int width, int height) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    gHost.parent = surface;
    gHost.width = width;
    gHost.height = height;
}

wl_surface* waylandHostParentSurface() { return gHost.parent; }
int waylandHostWidth() { return gHost.width; }
int waylandHostHeight() { return gHost.height; }

void waylandHostCommitParent() {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    if (gHost.parent != nullptr) {
        wl_surface_commit(gHost.parent);
        wl_display_flush(gHost.display);
    }
}

void waylandHostSetOverlaySubsurface(wl_subsurface* subsurface, wl_surface* surface) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    gHost.overlay = subsurface;
    gHost.overlaySurface = surface;
}

void waylandHostSetLayerInfoCallback(LayerInfoCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    gLayerInfoCallback = callback;
}

bool waylandHostLayerInfo(ANativeWindow* window, int* x, int* y, int* width, int* height,
                          int* z, float* alpha, bool* shareable) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    if (gLayerInfoCallback == nullptr) {
        return false;
    }
    return gLayerInfoCallback(window, x, y, width, height, z, alpha, shareable);
}

void waylandHostSetLayerSharedCallback(LayerSharedCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    gLayerSharedCallback = callback;
}

void waylandHostRecordSwap() { gSwapCount.fetch_add(1, std::memory_order_relaxed); }
uint64_t waylandHostSwapCount() { return gSwapCount.load(std::memory_order_relaxed); }

void waylandHostSetLayerShared(ANativeWindow* window, bool shared) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    if (gLayerSharedCallback != nullptr) {
        gLayerSharedCallback(window, shared);
    }
}

void waylandHostRegisterChild(wl_subsurface* subsurface, wl_surface* childSurface, int z) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    if (subsurface == nullptr || childSurface == nullptr) {
        return;
    }
    gChildren.push_back(ChildSurface{subsurface, childSurface, z});
    reorderChildrenLocked();
    if (std::getenv("DROIDLET_COMPOSER_DEBUG") != nullptr) {
        std::fprintf(stderr, "wayland-host: register child %p surface %p z=%d total=%zu\n",
                     static_cast<void*>(subsurface), static_cast<void*>(childSurface), z,
                     gChildren.size());
        for (const auto& child : gChildren) {
            std::fprintf(stderr, "  child %p surface %p z=%d\n",
                         static_cast<void*>(child.subsurface),
                         static_cast<void*>(child.surface), child.z);
        }
    }
}

void waylandHostRaiseOverlay() {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    reorderChildrenLocked();
}

void waylandHostSetEffectSubsurface(wl_subsurface* subsurface, wl_surface* surface, int z) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    gHost.effect = subsurface;
    gHost.effectSurface = surface;
    gHost.effectZ = z;
    reorderChildrenLocked();
}

void waylandHostSetEffectZ(int z) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    if (gHost.effectZ != z) {
        gHost.effectZ = z;
        reorderChildrenLocked();
    }
}

void waylandHostUpdateChildZ(wl_subsurface* subsurface, wl_surface* childSurface, int z) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    for (auto& child : gChildren) {
        if (child.subsurface == subsurface) {
            if (child.z != z) {
                child.z = z;
                reorderChildrenLocked();
            }
            return;
        }
    }
    waylandHostRegisterChild(subsurface, childSurface, z);
}

void waylandHostUnregisterChild(wl_subsurface* subsurface) {
    std::lock_guard<std::recursive_mutex> lock(waylandHostMutex());
    for (auto it = gChildren.begin(); it != gChildren.end(); ++it) {
        if (it->subsurface == subsurface) {
            gChildren.erase(it);
            reorderChildrenLocked();
            return;
        }
    }
}

}  // namespace droidlet
