// SPDX-License-Identifier: MIT

#include "omsi/render/device.hpp"

#include <algorithm>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

#include <volk.h>
#include <vulkan/vulkan.h>

namespace omsi::render {
namespace {

// volkInitialize must be called once before any other Vulkan call. It returns false when
// the loader DLL is missing, which is a different failure from "the driver has no device".
bool ensureLoaded(std::string& error) {
    static int state = -1;  // -1 unknown, 0 failed, 1 ready
    if (state < 0) {
        state = (volkInitialize() == VK_SUCCESS) ? 1 : 0;
    }
    if (state == 0 && error.empty()) {
        error = "the Vulkan loader could not be initialised (is vulkan-1.dll installed?)";
    }
    return state == 1;
}

std::string deviceName(const VkPhysicalDeviceProperties& props) {
    // deviceName is a fixed-size char array that is not necessarily NUL-terminated.
    std::string name(props.deviceName, strnlen(props.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE));
    while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) {
        name.pop_back();
    }
    return name;
}

}  // namespace

std::string formatApiVersion(std::uint32_t version) {
    std::ostringstream out;
    out << VK_API_VERSION_MAJOR(version) << '.' << VK_API_VERSION_MINOR(version) << '.'
        << VK_API_VERSION_PATCH(version);
    return out.str();
}

std::string formatDriverVersion(std::uint32_t version) {
    // The loader packs this as (vendor << 22) | minor << 12 | patch.
    std::ostringstream out;
    out << (version >> 22) << '.' << ((version >> 12) & 0x3FF) << '.' << (version & 0xFFF);
    return out.str();
}

std::uint32_t loaderVersion() {
    std::string error;
    if (!ensureLoaded(error)) {
        return 0;
    }
    // volk leaves this null when the loader is Vulkan 1.0, where the call does not exist;
    // calling it anyway is an access violation, not a zero.
    if (vkEnumerateInstanceVersion == nullptr) {
        return VK_API_VERSION_1_0;
    }
    std::uint32_t version = 0;
    vkEnumerateInstanceVersion(&version);
    return version;
}

bool loaderAvailable(std::string& error) {
    return ensureLoaded(error);
}
std::vector<Gpu> enumerateGpus(std::string& error) {
    std::vector<Gpu> gpus;
    if (!ensureLoaded(error)) {
        return gpus;
    }

    // A headless survey still needs an instance; one with no layers or extensions.
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "evigit";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "evigit";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    // Ask for 1.0 so an old loader still runs; what is really available is below.
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult result = vkCreateInstance(&create, nullptr, &instance);
    if (result != VK_SUCCESS) {
        error = "vkCreateInstance failed with VkResult " + std::to_string(result);
        return gpus;
    }

    // The instance has to exist before volk can load the per-instance entry points;
    // without this the device calls below dispatch through null pointers.
    volkLoadInstance(instance);

    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
        error = "the loader found no Vulkan device (install a GPU driver with Vulkan support)";
        vkDestroyInstance(instance, nullptr);
        return gpus;
    }

    std::vector<VkPhysicalDevice> handles(count);
    vkEnumeratePhysicalDevices(instance, &count, handles.data());

    for (const VkPhysicalDevice handle : handles) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(handle, &props);

        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(handle, &mem);

        Gpu gpu;
        gpu.name = deviceName(props);
        gpu.vendorId = props.vendorID;
        gpu.deviceId = props.deviceID;
        gpu.driverVersion = props.driverVersion;
        gpu.apiVersion = props.apiVersion;
        gpu.isDiscrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        gpu.isIntegrated = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        gpu.isCpu = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        gpu.maxTextureDimension2D = props.limits.maxImageDimension2D;
        gpu.maxUniformBufferRange = props.limits.maxUniformBufferRange;
        gpu.maxPushConstantsSize = props.limits.maxPushConstantsSize;
        gpu.maxDrawIndirectCount = props.limits.maxDrawIndirectCount;

        // The largest device-local heap is what a renderer budgets textures against. The
        // driver's own budget needs VK_EXT_memory_budget, which is not worth the extra
        // structure chain for a survey.
        for (std::uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
            const VkMemoryHeap& heap = mem.memoryHeaps[i];
            if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
                continue;
            }
            // Not std::max: vulkan.h with VK_USE_PLATFORM_WIN32_KHR pulls in windows.h, which
        // defines a max() macro that breaks the qualified name.
        if (heap.size > gpu.heapSize) {
            gpu.heapSize = heap.size;
        }
        }

        gpu.supportsSamplerAnisotropy = props.limits.maxSamplerAnisotropy > 1.0f;
        gpu.supportsSeparateDepthStencilLayouts = props.apiVersion >= VK_API_VERSION_1_2;

        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(handle, &features);
        gpu.supportsGeometryShader = features.geometryShader == VK_TRUE;

        std::uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(handle, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        if (extensionCount > 0) {
            vkEnumerateDeviceExtensionProperties(handle, nullptr, &extensionCount,
                                                  extensions.data());
        }
        // The macro for this extension name is only defined when the beta extensions are
        // enabled, so the literal is used instead.
        for (const VkExtensionProperties& extension : extensions) {
            if (std::strcmp(extension.extensionName, "VK_EXT_timeline_semaphore") == 0) {
                gpu.supportsTimelineSemaphore = true;
            }
        }

        gpus.push_back(std::move(gpu));
    }

    vkDestroyInstance(instance, nullptr);

    // Discrete first: that is the card a renderer would pick.
    std::stable_sort(gpus.begin(), gpus.end(),
                     [](const Gpu& a, const Gpu& b) { return a.isDiscrete && !b.isDiscrete; });
    return gpus;
}

std::string summarize(const std::vector<Gpu>& gpus) {
    if (gpus.empty()) {
        return "no Vulkan device\n";
    }
    std::ostringstream out;
    for (const Gpu& gpu : gpus) {
        const char* kind = gpu.isDiscrete
                               ? "discrete"
                               : (gpu.isIntegrated ? "integrated" : (gpu.isCpu ? "cpu" : "other"));
        char line[512];
        std::snprintf(line, sizeof(line),
                      "%-42s %-10s api %-7s driver %-12s  local %7.1f MB  tex2d %u\n",
                      gpu.name.c_str(), kind, formatApiVersion(gpu.apiVersion).c_str(),
                      formatDriverVersion(gpu.driverVersion).c_str(),
                      static_cast<double>(gpu.heapSize) / (1024.0 * 1024.0),
                      gpu.maxTextureDimension2D);
        out << line;
    }
    return out.str();
}

}  // namespace omsi::render