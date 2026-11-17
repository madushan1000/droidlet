/*
 * Host Vulkan bridge for Android NDK applications.
 *
 * Desktop Vulkan has no VK_KHR_android_surface.  Native apps therefore cannot
 * create a swapchain from an ANativeWindow.  bionic_translation prefers
 * `bionic_`-prefixed host symbols when relocating application libraries, so
 * this file provides the Android surface entry points and redirects them to a
 * headless Vulkan surface.  vkQueuePresentKHR copies the presented image into
 * the Android window buffer, just as the EGL bridge copies GL frames.
 */

#define VK_NO_PROTOTYPES 1

#include <android/native_window.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <dlfcn.h>

#include "wayland_host.h"

#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char* kAndroidSurfaceExtension = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;

bool debugEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("DROIDLET_VULKAN_DEBUG");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

#define VKLOG(...) do { if (debugEnabled()) std::fprintf(stderr, "droidlet-vk: " __VA_ARGS__); } while (0)

struct Loader {
    void* handle = nullptr;
    bool tried = false;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties enumerateInstanceExtensions = nullptr;
};

Loader gLoader;
std::mutex gMutex;

struct SurfaceInfo {
    ANativeWindow* window;
};

struct SwapchainInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
};

struct QueueInfo {
    VkDevice device = VK_NULL_HANDLE;
    uint32_t family = 0;
};

struct DeviceInfo {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
};

struct CopyResources {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
};

std::unordered_map<uint64_t, SurfaceInfo> gSurfaces;
std::unordered_map<uint64_t, SwapchainInfo> gSwapchains;
std::unordered_map<uint64_t, QueueInfo> gQueues;
std::unordered_map<uint64_t, DeviceInfo> gDevices;
std::unordered_map<uint64_t, CopyResources> gCopyResources;
bool gHeadlessSurfaceAvailable = true;

template <typename T>
uint64_t handleKey(T handle) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
}

bool ensureLoader() {
    if (gLoader.tried) {
        return gLoader.handle != nullptr;
    }
    gLoader.tried = true;
    gLoader.handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (gLoader.handle == nullptr) {
        gLoader.handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (gLoader.handle == nullptr) {
        return false;
    }
    gLoader.getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            dlsym(gLoader.handle, "vkGetInstanceProcAddr"));
    gLoader.getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            dlsym(gLoader.handle, "vkGetDeviceProcAddr"));
    gLoader.createInstance = reinterpret_cast<PFN_vkCreateInstance>(
            dlsym(gLoader.handle, "vkCreateInstance"));
    gLoader.enumerateInstanceExtensions =
            reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
                    dlsym(gLoader.handle, "vkEnumerateInstanceExtensionProperties"));
    return gLoader.getInstanceProcAddr != nullptr && gLoader.createInstance != nullptr;
}

PFN_vkVoidFunction realGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!ensureLoader() || gLoader.getInstanceProcAddr == nullptr) {
        return nullptr;
    }
    return gLoader.getInstanceProcAddr(instance, name);
}

PFN_vkVoidFunction realGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!ensureLoader() || gLoader.getDeviceProcAddr == nullptr) {
        return nullptr;
    }
    return gLoader.getDeviceProcAddr(device, name);
}

template <typename T>
T resolveInstance(VkInstance instance, const char* name) {
    return reinterpret_cast<T>(realGetInstanceProcAddr(instance, name));
}

template <typename T>
T resolveDevice(VkDevice device, const char* name) {
    return reinterpret_cast<T>(realGetDeviceProcAddr(device, name));
}

VkResult droidlet_vkCreateInstance(const VkInstanceCreateInfo* createInfo,
                                   const VkAllocationCallbacks* allocator, VkInstance* instance);
VkResult droidlet_vkEnumerateInstanceExtensionProperties(
        const char* layerName, uint32_t* propertyCount, VkExtensionProperties* properties);
VkResult droidlet_vkCreateAndroidSurfaceKHR(VkInstance instance,
        const VkAndroidSurfaceCreateInfoKHR* createInfo,
        const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);
PFN_vkVoidFunction droidlet_vkGetDeviceProcAddr(VkDevice device, const char* name);
VkResult droidlet_vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
        const VkAllocationCallbacks* allocator);
VkResult droidlet_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator);
VkResult droidlet_vkCreateDevice(VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* createInfo, const VkAllocationCallbacks* allocator,
        VkDevice* device);
VkResult droidlet_vkCreateSwapchainKHR(VkDevice device,
        const VkSwapchainCreateInfoKHR* createInfo, const VkAllocationCallbacks* allocator,
        VkSwapchainKHR* swapchain);
VkResult droidlet_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
        const VkAllocationCallbacks* allocator);
VkResult droidlet_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo);
void droidlet_vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue);
void droidlet_vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue);
VkResult droidlet_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
        uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex);
VkResult droidlet_vkQueueSubmit(VkQueue queue, uint32_t submitCount,
        const VkSubmitInfo* submits, VkFence fence);
VkResult droidlet_vkWaitForFences(VkDevice device, uint32_t fenceCount,
        const VkFence* fences, VkBool32 waitAll, uint64_t timeout);

PFN_vkVoidFunction droidlet_vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    if (name[0] == 'v' && name[1] == 'k') {
        VKLOG("GIPA %s\n", name);
    }
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkGetInstanceProcAddr);
    }
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkGetDeviceProcAddr);
    }
    if (std::strcmp(name, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkDestroyDevice);
    }
    if (std::strcmp(name, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkCreateInstance);
    }
    if (std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(
                &droidlet_vkEnumerateInstanceExtensionProperties);
    }
    if (std::strcmp(name, "vkCreateAndroidSurfaceKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkCreateAndroidSurfaceKHR);
    }
    if (std::strcmp(name, "vkDestroySurfaceKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkDestroySurfaceKHR);
    }
    if (std::strcmp(name, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkCreateDevice);
    }
    if (std::strcmp(name, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkDestroyDevice);
    }
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkCreateSwapchainKHR);
    }
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkDestroySwapchainKHR);
    }
    if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkQueuePresentKHR);
    }
    if (std::strcmp(name, "vkGetDeviceQueue") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkGetDeviceQueue);
    }
    if (std::strcmp(name, "vkGetDeviceQueue2") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkGetDeviceQueue2);
    }
    if (std::strcmp(name, "vkAcquireNextImageKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkAcquireNextImageKHR);
    }
    if (std::strcmp(name, "vkQueueSubmit") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkQueueSubmit);
    }
    if (std::strcmp(name, "vkWaitForFences") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkWaitForFences);
    }
    return realGetInstanceProcAddr(instance, name);
}

PFN_vkVoidFunction droidlet_vkGetDeviceProcAddr(VkDevice device, const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    if (name[0] == 'v' && name[1] == 'k') {
        VKLOG("GDPA %s\n", name);
    }
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkGetDeviceProcAddr);
    }
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkCreateSwapchainKHR);
    }
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkDestroySwapchainKHR);
    }
    if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkQueuePresentKHR);
    }
    if (std::strcmp(name, "vkGetDeviceQueue") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkGetDeviceQueue);
    }
    if (std::strcmp(name, "vkGetDeviceQueue2") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkGetDeviceQueue2);
    }
    if (std::strcmp(name, "vkAcquireNextImageKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkAcquireNextImageKHR);
    }
    if (std::strcmp(name, "vkQueueSubmit") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkQueueSubmit);
    }
    if (std::strcmp(name, "vkWaitForFences") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&droidlet_vkWaitForFences);
    }
    return realGetDeviceProcAddr(device, name);
}

VkResult droidlet_vkEnumerateInstanceExtensionProperties(
        const char* layerName, uint32_t* propertyCount, VkExtensionProperties* properties) {
    if (!ensureLoader() || gLoader.enumerateInstanceExtensions == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (layerName != nullptr) {
        return gLoader.enumerateInstanceExtensions(layerName, propertyCount, properties);
    }
    uint32_t realCount = 0;
    VkResult result = gLoader.enumerateInstanceExtensions(nullptr, &realCount, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    const uint32_t wanted = realCount + 1;
    if (properties == nullptr) {
        *propertyCount = wanted;
        return VK_SUCCESS;
    }
    if (*propertyCount < wanted) {
        *propertyCount = wanted;
        return VK_INCOMPLETE;
    }
    std::vector<VkExtensionProperties> realExtensions(realCount);
    result = gLoader.enumerateInstanceExtensions(nullptr, &realCount, realExtensions.data());
    if (result != VK_SUCCESS) {
        return result;
    }
    std::memcpy(properties, realExtensions.data(),
            realCount * sizeof(VkExtensionProperties));
    VkExtensionProperties androidExtension{};
    std::strncpy(androidExtension.extensionName, kAndroidSurfaceExtension,
            sizeof(androidExtension.extensionName) - 1);
    androidExtension.specVersion = VK_KHR_ANDROID_SURFACE_SPEC_VERSION;
    properties[realCount] = androidExtension;
    *propertyCount = wanted;
    return VK_SUCCESS;
}

VkResult droidlet_vkCreateInstance(const VkInstanceCreateInfo* createInfo,
                                   const VkAllocationCallbacks* allocator, VkInstance* instance) {
    if (!ensureLoader() || gLoader.createInstance == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (createInfo == nullptr) {
        return gLoader.createInstance(createInfo, allocator, instance);
    }
    std::vector<const char*> extensions;
    bool wantsAndroidSurface = false;
    bool hasHeadless = false;
    for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
        const char* name = createInfo->ppEnabledExtensionNames[i];
        if (name == nullptr) {
            continue;
        }
        if (std::strcmp(name, kAndroidSurfaceExtension) == 0) {
            // The host loader does not know the Android surface extension; the
            // bridge implements it on top of VK_EXT_headless_surface.
            wantsAndroidSurface = true;
            continue;
        }
        if (std::strcmp(name, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME) == 0) {
            hasHeadless = true;
        }
        extensions.push_back(name);
    }
    if (!wantsAndroidSurface) {
        return gLoader.createInstance(createInfo, allocator, instance);
    }
    if (!hasHeadless) {
        extensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
    }
    VkInstanceCreateInfo adjusted = *createInfo;
    adjusted.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    adjusted.ppEnabledExtensionNames = extensions.data();
    VkResult result = gLoader.createInstance(&adjusted, allocator, instance);
    if (result == VK_ERROR_EXTENSION_NOT_PRESENT && !hasHeadless) {
        // Older loaders without VK_EXT_headless_surface: create the instance
        // without it; vkCreateAndroidSurfaceKHR will report the gap.
        gHeadlessSurfaceAvailable = false;
        adjusted.enabledExtensionCount = static_cast<uint32_t>(extensions.size() - 1);
        result = gLoader.createInstance(&adjusted, allocator, instance);
    }
    VKLOG("create instance result=%d extensions=%u android-bridge=%s\n", result,
            adjusted.enabledExtensionCount, result == VK_SUCCESS ? "on" : "off");
    return result;
}

VkResult droidlet_vkCreateAndroidSurfaceKHR(VkInstance instance,
        const VkAndroidSurfaceCreateInfoKHR* createInfo,
        const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    if (createInfo == nullptr || createInfo->window == nullptr || surface == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto createHeadless = resolveInstance<PFN_vkCreateHeadlessSurfaceEXT>(
            instance, "vkCreateHeadlessSurfaceEXT");
    if (createHeadless == nullptr || !gHeadlessSurfaceAvailable) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    VkHeadlessSurfaceCreateInfoEXT headlessInfo{};
    headlessInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    VkSurfaceKHR created = VK_NULL_HANDLE;
    VkResult result = createHeadless(instance, &headlessInfo, allocator, &created);
    if (result != VK_SUCCESS) {
        return result;
    }
    ANativeWindow_acquire(createInfo->window);
    {
        std::lock_guard lock(gMutex);
        gSurfaces[handleKey(created)] = SurfaceInfo{createInfo->window};
    }
    VKLOG("create android surface %p window=%p\n", (void*) (uintptr_t) created,
            (void*) createInfo->window);
    *surface = created;
    return VK_SUCCESS;
}

VkResult droidlet_vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
        const VkAllocationCallbacks* allocator) {
    auto destroy = resolveInstance<PFN_vkDestroySurfaceKHR>(instance, "vkDestroySurfaceKHR");
    ANativeWindow* window = nullptr;
    {
        std::lock_guard lock(gMutex);
        auto it = gSurfaces.find(handleKey(surface));
        if (it != gSurfaces.end()) {
            window = it->second.window;
            gSurfaces.erase(it);
        }
    }
    if (destroy != nullptr) {
        destroy(instance, surface, allocator);
    }
    if (window != nullptr) {
        ANativeWindow_release(window);
    }
    return VK_SUCCESS;
}

VkResult droidlet_vkCreateDevice(VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* createInfo, const VkAllocationCallbacks* allocator,
        VkDevice* device) {
    ensureLoader();
    auto create = resolveInstance<PFN_vkCreateDevice>(VK_NULL_HANDLE, "vkCreateDevice");
    if (create == nullptr) {
        // vkCreateDevice is an instance-level function; the instance is not
        // available here, so go through the loader's global dispatch.
        create = reinterpret_cast<PFN_vkCreateDevice>(
                dlsym(gLoader.handle, "vkCreateDevice"));
    }
    if (create == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = create(physicalDevice, createInfo, allocator, device);
    if (result == VK_SUCCESS && device != nullptr) {
        std::lock_guard lock(gMutex);
        gDevices[handleKey(*device)] = DeviceInfo{physicalDevice};
    }
    return result;
}

VkResult droidlet_vkCreateSwapchainKHR(VkDevice device,
        const VkSwapchainCreateInfoKHR* createInfo, const VkAllocationCallbacks* allocator,
        VkSwapchainKHR* swapchain) {
    auto create = resolveDevice<PFN_vkCreateSwapchainKHR>(device, "vkCreateSwapchainKHR");
    if (create == nullptr || createInfo == nullptr || swapchain == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSwapchainCreateInfoKHR adjusted = *createInfo;
    adjusted.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkResult result = create(device, &adjusted, allocator, swapchain);
    if (result == VK_SUCCESS) {
        std::lock_guard lock(gMutex);
        gSwapchains[handleKey(*swapchain)] = SwapchainInfo{
                device, createInfo->surface, createInfo->imageFormat, createInfo->imageExtent};
        VKLOG("swapchain created format=%d extent=%ux%u usage=0x%x\n",
                createInfo->imageFormat, createInfo->imageExtent.width,
                createInfo->imageExtent.height, adjusted.imageUsage);
    }
    return result;
}

void destroyCopyResources(VkDevice device, CopyResources& resources) {
    auto destroyFence = resolveDevice<PFN_vkDestroyFence>(device, "vkDestroyFence");
    auto destroyBuffer = resolveDevice<PFN_vkDestroyBuffer>(device, "vkDestroyBuffer");
    auto freeMemory = resolveDevice<PFN_vkFreeMemory>(device, "vkFreeMemory");
    auto destroyPool = resolveDevice<PFN_vkDestroyCommandPool>(device, "vkDestroyCommandPool");
    if (destroyFence != nullptr && resources.fence != VK_NULL_HANDLE) {
        destroyFence(device, resources.fence, nullptr);
    }
    if (destroyBuffer != nullptr && resources.buffer != VK_NULL_HANDLE) {
        destroyBuffer(device, resources.buffer, nullptr);
    }
    if (freeMemory != nullptr && resources.memory != VK_NULL_HANDLE) {
        freeMemory(device, resources.memory, nullptr);
    }
    if (destroyPool != nullptr && resources.pool != VK_NULL_HANDLE) {
        destroyPool(device, resources.pool, nullptr);
    }
    resources = CopyResources{};
}

VkResult droidlet_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
        const VkAllocationCallbacks* allocator) {
    auto destroy = resolveDevice<PFN_vkDestroySwapchainKHR>(device, "vkDestroySwapchainKHR");
    CopyResources resources;
    {
        std::lock_guard lock(gMutex);
        auto it = gCopyResources.find(handleKey(swapchain));
        if (it != gCopyResources.end()) {
            resources = std::move(it->second);
            gCopyResources.erase(it);
        }
        gSwapchains.erase(handleKey(swapchain));
    }
    destroyCopyResources(device, resources);
    if (destroy != nullptr) {
        destroy(device, swapchain, allocator);
    }
    return VK_SUCCESS;
}

VkResult droidlet_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    std::vector<CopyResources> resources;
    {
        std::lock_guard lock(gMutex);
        for (auto it = gCopyResources.begin(); it != gCopyResources.end();) {
            auto swapchainIt = gSwapchains.find(it->first);
            if (swapchainIt != gSwapchains.end() && swapchainIt->second.device == device) {
                resources.push_back(std::move(it->second));
                gSwapchains.erase(swapchainIt);
                it = gCopyResources.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = gQueues.begin(); it != gQueues.end();) {
            it = it->second.device == device ? gQueues.erase(it) : std::next(it);
        }
        gDevices.erase(handleKey(device));
    }
    for (auto& resource : resources) {
        destroyCopyResources(device, resource);
    }
    auto destroy = resolveDevice<PFN_vkDestroyDevice>(device, "vkDestroyDevice");
    if (destroy != nullptr) {
        destroy(device, allocator);
    }
    return VK_SUCCESS;
}

void droidlet_vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue) {
    auto getQueue = resolveDevice<PFN_vkGetDeviceQueue>(device, "vkGetDeviceQueue");
    if (getQueue == nullptr) {
        return;
    }
    getQueue(device, family, index, queue);
    if (queue != nullptr && *queue != VK_NULL_HANDLE) {
        VKLOG("get device queue family=%u queue=%p\n", family, (void*) *queue);
        std::lock_guard lock(gMutex);
        gQueues[handleKey(*queue)] = QueueInfo{device, family};
    }
}

void droidlet_vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue) {
    auto getQueue = resolveDevice<PFN_vkGetDeviceQueue2>(device, "vkGetDeviceQueue2");
    if (getQueue == nullptr) {
        return;
    }
    getQueue(device, info, queue);
    if (queue != nullptr && *queue != VK_NULL_HANDLE && info != nullptr) {
        std::lock_guard lock(gMutex);
        gQueues[handleKey(*queue)] = QueueInfo{device, info->queueFamilyIndex};
    }
}

bool ensureCopyResources(VkDevice device, uint32_t family, VkSwapchainKHR swapchain,
        const SwapchainInfo& info, CopyResources& resources) {
    if (resources.buffer != VK_NULL_HANDLE && resources.format == info.format &&
            resources.extent.width == info.extent.width &&
            resources.extent.height == info.extent.height) {
        return true;
    }
    destroyCopyResources(device, resources);

    auto createPool = resolveDevice<PFN_vkCreateCommandPool>(device, "vkCreateCommandPool");
    auto allocate = resolveDevice<PFN_vkAllocateCommandBuffers>(device,
            "vkAllocateCommandBuffers");
    auto createFence = resolveDevice<PFN_vkCreateFence>(device, "vkCreateFence");
    auto createBuffer = resolveDevice<PFN_vkCreateBuffer>(device, "vkCreateBuffer");
    auto getRequirements = resolveDevice<PFN_vkGetBufferMemoryRequirements>(device,
            "vkGetBufferMemoryRequirements");
    auto allocateMemory = resolveDevice<PFN_vkAllocateMemory>(device, "vkAllocateMemory");
    auto bindBuffer = resolveDevice<PFN_vkBindBufferMemory>(device, "vkBindBufferMemory");
    auto mapMemory = resolveDevice<PFN_vkMapMemory>(device, "vkMapMemory");
    auto getImages = resolveDevice<PFN_vkGetSwapchainImagesKHR>(device,
            "vkGetSwapchainImagesKHR");
    if (createPool == nullptr || allocate == nullptr || createFence == nullptr ||
            createBuffer == nullptr || getRequirements == nullptr ||
            allocateMemory == nullptr || bindBuffer == nullptr || mapMemory == nullptr ||
            getImages == nullptr) {
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family;
    if (createPool(device, &poolInfo, nullptr, &resources.pool) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = resources.pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if (allocate(device, &commandInfo, &resources.commandBuffer) != VK_SUCCESS) {
        destroyCopyResources(device, resources);
        return false;
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (createFence(device, &fenceInfo, nullptr, &resources.fence) != VK_SUCCESS) {
        destroyCopyResources(device, resources);
        return false;
    }

    resources.size = static_cast<VkDeviceSize>(info.extent.width) * info.extent.height * 4;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = resources.size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (createBuffer(device, &bufferInfo, nullptr, &resources.buffer) != VK_SUCCESS) {
        destroyCopyResources(device, resources);
        return false;
    }
    VkMemoryRequirements requirements{};
    getRequirements(device, resources.buffer, &requirements);

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    {
        std::lock_guard lock(gMutex);
        auto it = gDevices.find(handleKey(device));
        if (it != gDevices.end()) {
            physicalDevice = it->second.physicalDevice;
        }
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        destroyCopyResources(device, resources);
        return false;
    }
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    auto getMemoryProperties = resolveInstance<PFN_vkGetPhysicalDeviceMemoryProperties>(
            VK_NULL_HANDLE, "vkGetPhysicalDeviceMemoryProperties");
    if (getMemoryProperties == nullptr) {
        getMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                dlsym(gLoader.handle, "vkGetPhysicalDeviceMemoryProperties"));
    }
    if (getMemoryProperties == nullptr) {
        destroyCopyResources(device, resources);
        return false;
    }
    getMemoryProperties(physicalDevice, &memoryProperties);
    uint32_t memoryType = 0;
    const VkMemoryPropertyFlags wanted =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    bool found = false;
    for (; memoryType < memoryProperties.memoryTypeCount; ++memoryType) {
        if ((requirements.memoryTypeBits & (1u << memoryType)) &&
                (memoryProperties.memoryTypes[memoryType].propertyFlags & wanted) == wanted) {
            found = true;
            break;
        }
    }
    if (!found) {
        destroyCopyResources(device, resources);
        return false;
    }
    VkMemoryAllocateInfo memoryInfo{};
    memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryInfo.allocationSize = requirements.size;
    memoryInfo.memoryTypeIndex = memoryType;
    if (allocateMemory(device, &memoryInfo, nullptr, &resources.memory) != VK_SUCCESS) {
        destroyCopyResources(device, resources);
        return false;
    }
    if (bindBuffer(device, resources.buffer, resources.memory, 0) != VK_SUCCESS ||
            mapMemory(device, resources.memory, 0, VK_WHOLE_SIZE, 0,
                    &resources.mapped) != VK_SUCCESS) {
        destroyCopyResources(device, resources);
        return false;
    }

    uint32_t imageCount = 0;
    if (getImages(device, swapchain, &imageCount, nullptr) != VK_SUCCESS || imageCount == 0) {
        destroyCopyResources(device, resources);
        return false;
    }
    resources.images.resize(imageCount);
    if (getImages(device, swapchain, &imageCount, resources.images.data()) != VK_SUCCESS) {
        destroyCopyResources(device, resources);
        return false;
    }
    resources.format = info.format;
    resources.extent = info.extent;
    return true;
}

void copyToWindow(CopyResources& resources, VkFormat format, VkExtent2D extent,
        ANativeWindow* window) {
    if (window == nullptr || resources.mapped == nullptr) {
        return;
    }
    ANativeWindow_Buffer buffer{};
    if (ANativeWindow_lock(window, &buffer, nullptr) != 0) {
        return;
    }
    const int width = std::min<int>(extent.width, buffer.width);
    const int height = std::min<int>(extent.height, buffer.height);
    const auto* source = static_cast<const uint8_t*>(resources.mapped);
    auto* destination = static_cast<uint8_t*>(buffer.bits);
    const int windowFormat = ANativeWindow_getFormat(window);
    VKLOG("window format=%d size=%dx%d stride=%d source=%dx%d swapchain-format=%d\n",
            windowFormat, buffer.width, buffer.height, buffer.stride, extent.width,
            extent.height, format);

    if (windowFormat == 5 && (format == VK_FORMAT_R5G6B5_UNORM_PACK16 ||
            format == VK_FORMAT_B5G6R5_UNORM_PACK16)) {
        for (int y = 0; y < height; ++y) {
            const auto* src = reinterpret_cast<const uint16_t*>(
                    source + static_cast<size_t>(y) * extent.width * 2);
            auto* dst = reinterpret_cast<uint8_t*>(
                    destination + static_cast<size_t>(y) * buffer.stride * 4);
            for (int x = 0; x < width; ++x) {
                const uint16_t pixel = src[x];
                dst[x * 4 + 0] = static_cast<uint8_t>((pixel & 0x1f) << 3);
                dst[x * 4 + 1] = static_cast<uint8_t>(((pixel >> 5) & 0x3f) << 2);
                dst[x * 4 + 2] = static_cast<uint8_t>(((pixel >> 11) & 0x1f) << 3);
                dst[x * 4 + 3] = 255;
            }
        }
    } else if (windowFormat == 5 && (format == VK_FORMAT_B8G8R8A8_UNORM ||
            format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM ||
            format == VK_FORMAT_R8G8B8A8_SRGB)) {
        const bool swizzle = format == VK_FORMAT_R8G8B8A8_UNORM ||
                format == VK_FORMAT_R8G8B8A8_SRGB;
        for (int y = 0; y < height; ++y) {
            const uint8_t* src = source + static_cast<size_t>(y) * extent.width * 4;
            auto* dst = destination + static_cast<size_t>(y) * buffer.stride * 4;
            if (!swizzle) {
                std::memcpy(dst, src, static_cast<size_t>(width) * 4);
            } else {
                for (int x = 0; x < width; ++x) {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
        }
    } else if (windowFormat == 4) {
        const bool source32 = format == VK_FORMAT_B8G8R8A8_UNORM ||
                format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM ||
                format == VK_FORMAT_R8G8B8A8_SRGB;
        const bool swizzle = format == VK_FORMAT_B8G8R8A8_UNORM ||
                format == VK_FORMAT_B8G8R8A8_SRGB;
        if (format == VK_FORMAT_R5G6B5_UNORM_PACK16 ||
                format == VK_FORMAT_B5G6R5_UNORM_PACK16) {
            for (int y = 0; y < height; ++y) {
                const auto* src = reinterpret_cast<const uint16_t*>(
                        source + static_cast<size_t>(y) * extent.width * 2);
                auto* dst = reinterpret_cast<uint16_t*>(
                        destination + static_cast<size_t>(y) * buffer.stride * 2);
                std::memcpy(dst, src, static_cast<size_t>(width) * 2);
            }
        } else if (source32) {
            for (int y = 0; y < height; ++y) {
                const uint8_t* src = source + static_cast<size_t>(y) * extent.width * 4;
                auto* dst = reinterpret_cast<uint16_t*>(
                        destination + static_cast<size_t>(y) * buffer.stride * 2);
                for (int x = 0; x < width; ++x) {
                    const uint8_t* pixel = src + x * 4;
                    const uint8_t r = swizzle ? pixel[2] : pixel[0];
                    const uint8_t g = pixel[1];
                    const uint8_t b = swizzle ? pixel[0] : pixel[2];
                    dst[x] = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                }
            }
        }
    }
    ANativeWindow_unlockAndPost(window);
}

void copyPresentedImage(VkQueue queue, const QueueInfo& queueInfo, VkSwapchainKHR swapchain,
        uint32_t imageIndex) {
    SwapchainInfo info;
    CopyResources resources;
    ANativeWindow* window = nullptr;
    {
        std::lock_guard lock(gMutex);
        auto swapchainIt = gSwapchains.find(handleKey(swapchain));
        if (swapchainIt == gSwapchains.end()) {
            return;
        }
        info = swapchainIt->second;
        auto surfaceIt = gSurfaces.find(handleKey(info.surface));
        if (surfaceIt != gSurfaces.end()) {
            window = surfaceIt->second.window;
        }
        auto resourceIt = gCopyResources.find(handleKey(swapchain));
        if (resourceIt == gCopyResources.end()) {
            resourceIt = gCopyResources.emplace(handleKey(swapchain), CopyResources{}).first;
        }
        resources = resourceIt->second;
    }
    if (window == nullptr) {
        return;
    }

    auto begin = resolveDevice<PFN_vkBeginCommandBuffer>(queueInfo.device, "vkBeginCommandBuffer");
    auto end = resolveDevice<PFN_vkEndCommandBuffer>(queueInfo.device, "vkEndCommandBuffer");
    auto reset = resolveDevice<PFN_vkResetCommandBuffer>(queueInfo.device, "vkResetCommandBuffer");
    auto barrier = resolveDevice<PFN_vkCmdPipelineBarrier>(queueInfo.device,
            "vkCmdPipelineBarrier");
    auto copyImage = resolveDevice<PFN_vkCmdCopyImageToBuffer>(queueInfo.device,
            "vkCmdCopyImageToBuffer");
    auto submit = resolveDevice<PFN_vkQueueSubmit>(queueInfo.device, "vkQueueSubmit");
    auto waitIdle = resolveDevice<PFN_vkQueueWaitIdle>(queueInfo.device, "vkQueueWaitIdle");
    auto resetFence = resolveDevice<PFN_vkResetFences>(queueInfo.device, "vkResetFences");
    auto waitFence = resolveDevice<PFN_vkWaitForFences>(queueInfo.device, "vkWaitForFences");
    if (begin == nullptr || end == nullptr || reset == nullptr || barrier == nullptr ||
            copyImage == nullptr || submit == nullptr || waitIdle == nullptr ||
            resetFence == nullptr || waitFence == nullptr) {
        return;
    }

    if (!ensureCopyResources(queueInfo.device, queueInfo.family, swapchain, info, resources)) {
        VKLOG("copy resources unavailable for swapchain %p\n", (void*) (uintptr_t) swapchain);
        std::lock_guard lock(gMutex);
        gCopyResources[handleKey(swapchain)] = resources;
        return;
    }
    if (imageIndex >= resources.images.size()) {
        std::lock_guard lock(gMutex);
        gCopyResources[handleKey(swapchain)] = resources;
        return;
    }

    // Rendering was submitted to this queue before present; make sure it is
    // complete before reading the image.
    waitIdle(queue);
    reset(resources.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (begin(resources.commandBuffer, &beginInfo) != VK_SUCCESS) {
        std::lock_guard lock(gMutex);
        gCopyResources[handleKey(swapchain)] = resources;
        return;
    }

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = resources.images[imageIndex];
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toTransfer.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier(resources.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {info.extent.width, info.extent.height, 1};
    copyImage(resources.commandBuffer, resources.images[imageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resources.buffer, 1, &region);

    VkImageMemoryBarrier toPresent = toTransfer;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    barrier(resources.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    if (end(resources.commandBuffer) == VK_SUCCESS) {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &resources.commandBuffer;
        resetFence(queueInfo.device, 1, &resources.fence);
        if (submit(queue, 1, &submitInfo, resources.fence) == VK_SUCCESS) {
            waitFence(queueInfo.device, 1, &resources.fence, VK_TRUE, UINT64_MAX);
            VKLOG("present copy image=%u format=%d extent=%ux%u queue=%p\n", imageIndex,
                    info.format, info.extent.width, info.extent.height, (void*) queue);
            copyToWindow(resources, info.format, info.extent, window);
        }
    }

    std::lock_guard lock(gMutex);
    gCopyResources[handleKey(swapchain)] = resources;
}

VkResult droidlet_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
        uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex) {
    auto acquire = resolveDevice<PFN_vkAcquireNextImageKHR>(device, "vkAcquireNextImageKHR");
    VkResult result = acquire != nullptr ? acquire(device, swapchain, timeout, semaphore, fence,
            imageIndex) : VK_ERROR_INITIALIZATION_FAILED;
    VKLOG("acquire next image swapchain=%p result=%d index=%u\n",
            (void*) (uintptr_t) swapchain, result, imageIndex != nullptr ? *imageIndex : 0);
    return result;
}

VkResult droidlet_vkQueueSubmit(VkQueue queue, uint32_t submitCount,
        const VkSubmitInfo* submits, VkFence fence) {
    QueueInfo queueInfo;
    bool haveQueue = false;
    {
        std::lock_guard lock(gMutex);
        auto it = gQueues.find(handleKey(queue));
        if (it != gQueues.end()) {
            queueInfo = it->second;
            haveQueue = true;
        }
    }
    PFN_vkQueueSubmit submit = nullptr;
    if (haveQueue) {
        submit = resolveDevice<PFN_vkQueueSubmit>(queueInfo.device, "vkQueueSubmit");
    } else if (gLoader.handle != nullptr) {
        submit = reinterpret_cast<PFN_vkQueueSubmit>(dlsym(gLoader.handle, "vkQueueSubmit"));
    }
    VKLOG("queue submit queue=%p count=%u fence=%p\n", (void*) queue, submitCount,
            (void*) fence);
    return submit != nullptr ? submit(queue, submitCount, submits, fence)
                             : VK_ERROR_INITIALIZATION_FAILED;
}

VkResult droidlet_vkWaitForFences(VkDevice device, uint32_t fenceCount,
        const VkFence* fences, VkBool32 waitAll, uint64_t timeout) {
    auto wait = resolveDevice<PFN_vkWaitForFences>(device, "vkWaitForFences");
    VKLOG("wait fences device=%p count=%u timeout=%llu\n", (void*) device, fenceCount,
            (unsigned long long) timeout);
    VkResult result = wait != nullptr ? wait(device, fenceCount, fences, waitAll, timeout)
                                      : VK_ERROR_INITIALIZATION_FAILED;
    VKLOG("wait fences result=%d\n", result);
    return result;
}

VkResult droidlet_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo) {
    VKLOG("queue present entry queue=%p swapchains=%u\n", (void*) queue,
            presentInfo != nullptr ? presentInfo->swapchainCount : 0);
    QueueInfo queueInfo;
    bool haveQueue = false;
    {
        std::lock_guard lock(gMutex);
        auto it = gQueues.find(handleKey(queue));
        if (it != gQueues.end()) {
            queueInfo = it->second;
            haveQueue = true;
        }
    }
    if (haveQueue && presentInfo != nullptr) {
        for (uint32_t i = 0; i < presentInfo->swapchainCount; ++i) {
            if (presentInfo->pImageIndices == nullptr) {
                continue;
            }
            copyPresentedImage(queue, queueInfo, presentInfo->pSwapchains[i],
                    presentInfo->pImageIndices[i]);
        }
    }
    PFN_vkQueuePresentKHR present = nullptr;
    if (haveQueue) {
        present = resolveDevice<PFN_vkQueuePresentKHR>(queueInfo.device, "vkQueuePresentKHR");
    } else if (gLoader.handle != nullptr) {
        present = reinterpret_cast<PFN_vkQueuePresentKHR>(
                dlsym(gLoader.handle, "vkQueuePresentKHR"));
    }
    if (present == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = present(queue, presentInfo);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        droidlet::waylandHostRecordSwap();
    }
    return result;
}

}  // namespace

extern "C" {

__attribute__((visibility("default")))
PFN_vkVoidFunction bionic_vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    return droidlet_vkGetInstanceProcAddr(instance, name);
}

__attribute__((visibility("default")))
PFN_vkVoidFunction bionic_vkGetDeviceProcAddr(VkDevice device, const char* name) {
    return droidlet_vkGetDeviceProcAddr(device, name);
}

__attribute__((visibility("default")))
VkResult bionic_vkCreateInstance(const VkInstanceCreateInfo* createInfo,
                                 const VkAllocationCallbacks* allocator, VkInstance* instance) {
    return droidlet_vkCreateInstance(createInfo, allocator, instance);
}

__attribute__((visibility("default")))
VkResult bionic_vkEnumerateInstanceExtensionProperties(const char* layerName,
        uint32_t* propertyCount, VkExtensionProperties* properties) {
    return droidlet_vkEnumerateInstanceExtensionProperties(layerName, propertyCount, properties);
}

__attribute__((visibility("default")))
VkResult bionic_vkCreateAndroidSurfaceKHR(VkInstance instance,
        const VkAndroidSurfaceCreateInfoKHR* createInfo,
        const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    return droidlet_vkCreateAndroidSurfaceKHR(instance, createInfo, allocator, surface);
}

__attribute__((visibility("default")))
VkResult bionic_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    return droidlet_vkDestroyDevice(device, allocator);
}

__attribute__((visibility("default")))
VkResult bionic_vkCreateDevice(VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* createInfo, const VkAllocationCallbacks* allocator,
        VkDevice* device) {
    return droidlet_vkCreateDevice(physicalDevice, createInfo, allocator, device);
}

__attribute__((visibility("default")))
VkResult bionic_vkCreateSwapchainKHR(VkDevice device,
        const VkSwapchainCreateInfoKHR* createInfo, const VkAllocationCallbacks* allocator,
        VkSwapchainKHR* swapchain) {
    return droidlet_vkCreateSwapchainKHR(device, createInfo, allocator, swapchain);
}

__attribute__((visibility("default")))
VkResult bionic_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
        const VkAllocationCallbacks* allocator) {
    return droidlet_vkDestroySwapchainKHR(device, swapchain, allocator);
}

__attribute__((visibility("default")))
VkResult bionic_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
        uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex) {
    return droidlet_vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence,
            imageIndex);
}

__attribute__((visibility("default")))
void bionic_vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue) {
    droidlet_vkGetDeviceQueue(device, family, index, queue);
}

__attribute__((visibility("default")))
void bionic_vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue) {
    droidlet_vkGetDeviceQueue2(device, info, queue);
}

__attribute__((visibility("default")))
VkResult bionic_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo) {
    return droidlet_vkQueuePresentKHR(queue, presentInfo);
}

}  // extern "C"
