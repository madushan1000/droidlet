/*
 * Minimal bionic Vulkan fixture: create a VK_KHR_android_surface from the
 * Java Surface's ANativeWindow, clear the swapchain to a solid colour and
 * present it.  Used to exercise the host Android-surface bridge.
 */

#define VK_NO_PROTOTYPES 1

#include <android/native_window_jni.h>
#include <jni.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD(name) \
    do { \
        pfn_##name = reinterpret_cast<PFN_##name>(dlsym(handle, #name)); \
        if (pfn_##name == nullptr) { \
            return -__LINE__; \
        } \
    } while (0)

namespace {

PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr;
PFN_vkGetDeviceProcAddr pfn_vkGetDeviceProcAddr;
PFN_vkCreateInstance pfn_vkCreateInstance;
PFN_vkEnumerateInstanceExtensionProperties pfn_vkEnumerateInstanceExtensionProperties;
PFN_vkEnumeratePhysicalDevices pfn_vkEnumeratePhysicalDevices;
PFN_vkGetPhysicalDeviceQueueFamilyProperties pfn_vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR pfn_vkGetPhysicalDeviceSurfaceSupportKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR pfn_vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR pfn_vkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkCreateDevice pfn_vkCreateDevice;
PFN_vkGetDeviceQueue pfn_vkGetDeviceQueue;
PFN_vkCreateSwapchainKHR pfn_vkCreateSwapchainKHR;
PFN_vkGetSwapchainImagesKHR pfn_vkGetSwapchainImagesKHR;
PFN_vkCreateCommandPool pfn_vkCreateCommandPool;
PFN_vkAllocateCommandBuffers pfn_vkAllocateCommandBuffers;
PFN_vkBeginCommandBuffer pfn_vkBeginCommandBuffer;
PFN_vkCmdPipelineBarrier pfn_vkCmdPipelineBarrier;
PFN_vkCmdClearColorImage pfn_vkCmdClearColorImage;
PFN_vkEndCommandBuffer pfn_vkEndCommandBuffer;
PFN_vkQueueSubmit pfn_vkQueueSubmit;
PFN_vkAcquireNextImageKHR pfn_vkAcquireNextImageKHR;
PFN_vkQueuePresentKHR pfn_vkQueuePresentKHR;
PFN_vkDeviceWaitIdle pfn_vkDeviceWaitIdle;
PFN_vkDestroySwapchainKHR pfn_vkDestroySwapchainKHR;
PFN_vkDestroyDevice pfn_vkDestroyDevice;
PFN_vkDestroySurfaceKHR pfn_vkDestroySurfaceKHR;
PFN_vkDestroyInstance pfn_vkDestroyInstance;
PFN_vkCreateAndroidSurfaceKHR pfn_vkCreateAndroidSurfaceKHR;

template <typename T>
T getInstance(VkInstance instance, const char* name) {
    return reinterpret_cast<T>(pfn_vkGetInstanceProcAddr(instance, name));
}

template <typename T>
T getDevice(VkDevice device, const char* name) {
    return reinterpret_cast<T>(pfn_vkGetDeviceProcAddr(device, name));
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_example_droidlet_vulkanfixture_Native_render(JNIEnv* env, jclass, jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return -1;
    }

    void* handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    }
    if (handle == nullptr) {
        ANativeWindow_release(window);
        return -2;
    }

    LOAD(vkGetInstanceProcAddr);
    LOAD(vkGetDeviceProcAddr);
    LOAD(vkCreateInstance);
    LOAD(vkEnumerateInstanceExtensionProperties);

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "droidlet-vulkan-fixture";
    applicationInfo.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = 2;
    instanceInfo.ppEnabledExtensionNames = extensions;
    VkInstance instance = VK_NULL_HANDLE;
    if (pfn_vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        ANativeWindow_release(window);
        return -3;
    }

    pfn_vkCreateAndroidSurfaceKHR = getInstance<PFN_vkCreateAndroidSurfaceKHR>(
            instance, "vkCreateAndroidSurfaceKHR");
    pfn_vkDestroySurfaceKHR = getInstance<PFN_vkDestroySurfaceKHR>(
            instance, "vkDestroySurfaceKHR");
    pfn_vkDestroyInstance = getInstance<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    pfn_vkEnumeratePhysicalDevices = getInstance<PFN_vkEnumeratePhysicalDevices>(
            instance, "vkEnumeratePhysicalDevices");
    pfn_vkGetPhysicalDeviceQueueFamilyProperties =
            getInstance<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
                    instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    pfn_vkGetPhysicalDeviceSurfaceSupportKHR =
            getInstance<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
                    instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR =
            getInstance<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
                    instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    pfn_vkGetPhysicalDeviceSurfaceFormatsKHR =
            getInstance<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
                    instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    pfn_vkGetPhysicalDeviceSurfacePresentModesKHR =
            getInstance<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
                    instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    pfn_vkCreateDevice = getInstance<PFN_vkCreateDevice>(instance, "vkCreateDevice");
    if (pfn_vkCreateAndroidSurfaceKHR == nullptr) {
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -4;
    }

    VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.window = window;
    VkSurfaceKHR vkSurface = VK_NULL_HANDLE;
    if (pfn_vkCreateAndroidSurfaceKHR(instance, &surfaceInfo, nullptr, &vkSurface) != VK_SUCCESS) {
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -5;
    }

    uint32_t physicalDeviceCount = 0;
    pfn_vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
    if (physicalDeviceCount == 0) {
        pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -6;
    }
    VkPhysicalDevice physicalDevices[8]{};
    if (physicalDeviceCount > 8) {
        physicalDeviceCount = 8;
    }
    pfn_vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    for (uint32_t d = 0; d < physicalDeviceCount && physicalDevice == VK_NULL_HANDLE; ++d) {
        uint32_t familyCount = 0;
        pfn_vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[d], &familyCount, nullptr);
        VkQueueFamilyProperties families[16]{};
        if (familyCount > 16) {
            familyCount = 16;
        }
        pfn_vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[d], &familyCount, families);
        for (uint32_t f = 0; f < familyCount; ++f) {
            VkBool32 supported = VK_FALSE;
            pfn_vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevices[d], f, vkSurface, &supported);
            if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supported) {
                physicalDevice = physicalDevices[d];
                queueFamily = f;
                break;
            }
        }
    }
    if (physicalDevice == VK_NULL_HANDLE) {
        pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -7;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, vkSurface, &capabilities);
    uint32_t formatCount = 0;
    pfn_vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, nullptr);
    VkSurfaceFormatKHR formats[32]{};
    if (formatCount > 32) {
        formatCount = 32;
    }
    pfn_vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, formats);
    uint32_t presentModeCount = 0;
    pfn_vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice, vkSurface, &presentModeCount, nullptr);
    VkPresentModeKHR presentModes[16]{};
    if (presentModeCount > 16) {
        presentModeCount = 16;
    }
    pfn_vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice, vkSurface, &presentModeCount, presentModes);
    if (formatCount == 0 || presentModeCount == 0) {
        pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -8;
    }

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (uint32_t i = 0; i < formatCount; ++i) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM ||
                formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosenFormat = formats[i];
            break;
        }
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    VkDevice device = VK_NULL_HANDLE;
    if (pfn_vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
        pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -9;
    }

    pfn_vkGetDeviceQueue = getDevice<PFN_vkGetDeviceQueue>(device, "vkGetDeviceQueue");
    pfn_vkAcquireNextImageKHR = getDevice<PFN_vkAcquireNextImageKHR>(
            device, "vkAcquireNextImageKHR");
    pfn_vkCreateSwapchainKHR = getDevice<PFN_vkCreateSwapchainKHR>(device, "vkCreateSwapchainKHR");
    pfn_vkGetSwapchainImagesKHR = getDevice<PFN_vkGetSwapchainImagesKHR>(
            device, "vkGetSwapchainImagesKHR");
    pfn_vkCreateCommandPool = getDevice<PFN_vkCreateCommandPool>(device, "vkCreateCommandPool");
    pfn_vkAllocateCommandBuffers = getDevice<PFN_vkAllocateCommandBuffers>(
            device, "vkAllocateCommandBuffers");
    pfn_vkBeginCommandBuffer = getDevice<PFN_vkBeginCommandBuffer>(device, "vkBeginCommandBuffer");
    pfn_vkCmdPipelineBarrier = getDevice<PFN_vkCmdPipelineBarrier>(device, "vkCmdPipelineBarrier");
    pfn_vkCmdClearColorImage = getDevice<PFN_vkCmdClearColorImage>(device, "vkCmdClearColorImage");
    pfn_vkEndCommandBuffer = getDevice<PFN_vkEndCommandBuffer>(device, "vkEndCommandBuffer");
    pfn_vkQueueSubmit = getDevice<PFN_vkQueueSubmit>(device, "vkQueueSubmit");
    pfn_vkQueuePresentKHR = getDevice<PFN_vkQueuePresentKHR>(device, "vkQueuePresentKHR");
    pfn_vkDeviceWaitIdle = getDevice<PFN_vkDeviceWaitIdle>(device, "vkDeviceWaitIdle");
    pfn_vkDestroySwapchainKHR = getDevice<PFN_vkDestroySwapchainKHR>(
            device, "vkDestroySwapchainKHR");
    pfn_vkDestroyDevice = getDevice<PFN_vkDestroyDevice>(device, "vkDestroyDevice");
    if (pfn_vkCreateSwapchainKHR == nullptr || pfn_vkQueuePresentKHR == nullptr) {
        pfn_vkDestroyDevice(device, nullptr);
        pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -10;
    }
    VkQueue queue = VK_NULL_HANDLE;
    pfn_vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = ANativeWindow_getWidth(window);
        extent.height = ANativeWindow_getHeight(window);
        if (extent.width < capabilities.minImageExtent.width) {
            extent.width = capabilities.minImageExtent.width;
        }
        if (extent.height < capabilities.minImageExtent.height) {
            extent.height = capabilities.minImageExtent.height;
        }
        if (extent.width > capabilities.maxImageExtent.width) {
            extent.width = capabilities.maxImageExtent.width;
        }
        if (extent.height > capabilities.maxImageExtent.height) {
            extent.height = capabilities.maxImageExtent.height;
        }
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = vkSurface;
    swapchainInfo.minImageCount = capabilities.minImageCount;
    swapchainInfo.imageFormat = chosenFormat.format;
    swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = presentModes[0];
    swapchainInfo.clipped = VK_TRUE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if (pfn_vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS) {
        pfn_vkDestroyDevice(device, nullptr);
        pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -11;
    }

    uint32_t imageCount = 0;
    pfn_vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    if (imageCount == 0) {
        pfn_vkDestroySwapchainKHR(device, swapchain, nullptr);
        pfn_vkDestroyDevice(device, nullptr);
        pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -12;
    }
    VkImage images[8]{};
    if (imageCount > 8) {
        imageCount = 8;
    }
    pfn_vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images);
    uint32_t imageIndex = 0;
    VkResult acquireResult = pfn_vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            VK_NULL_HANDLE, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        pfn_vkDestroySwapchainKHR(device, swapchain, nullptr);
        pfn_vkDestroyDevice(device, nullptr);
        pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
        pfn_vkDestroyInstance(instance, nullptr);
        ANativeWindow_release(window);
        return -13;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily;
    VkCommandPool pool = VK_NULL_HANDLE;
    pfn_vkCreateCommandPool(device, &poolInfo, nullptr, &pool);
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    pfn_vkAllocateCommandBuffers(device, &commandInfo, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfn_vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = images[imageIndex];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pfn_vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkClearColorValue clearColor{{1.0f, 0.4f, 0.1f, 1.0f}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    pfn_vkCmdClearColorImage(commandBuffer, images[imageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
    VkImageMemoryBarrier presentBarrier = barrier;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    presentBarrier.dstAccessMask = 0;
    pfn_vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &presentBarrier);
    pfn_vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    pfn_vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    pfn_vkDeviceWaitIdle(device);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presentResult = pfn_vkQueuePresentKHR(queue, &presentInfo);
    pfn_vkDeviceWaitIdle(device);

    pfn_vkDestroySwapchainKHR(device, swapchain, nullptr);
    pfn_vkDestroyDevice(device, nullptr);
    pfn_vkDestroySurfaceKHR(instance, vkSurface, nullptr);
    pfn_vkDestroyInstance(instance, nullptr);
    ANativeWindow_release(window);
    dlclose(handle);
    return presentResult == VK_SUCCESS ? 0 : -14;
}
