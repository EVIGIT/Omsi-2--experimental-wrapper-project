// SPDX-License-Identifier: MIT
//
// What the machine's Vulkan driver can actually do.
//
// This runs before any renderer exists, on purpose: the renderer has to be written against
// what the driver supports rather than what the specification allows, and openOMSI's
// experience shows how deep that goes (its renderer carries a downlevel profile because
// Intel's Windows Vulkan driver crashes in igvk64.dll). A team can agree on a target
// Vulkan version only after seeing this output.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace omsi::render {

// One physical device, as the loader reports it.
struct Gpu {
    std::string name;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t driverVersion = 0;
    std::uint32_t apiVersion = 0;
    bool isDiscrete = false;
    bool isIntegrated = false;
    bool isCpu = false;

    std::uint32_t maxTextureDimension2D = 0;
    std::uint32_t maxUniformBufferRange = 0;
    std::uint32_t maxPushConstantsSize = 0;
    // Vulkan exposes no separate limit for the size of a draw; the closest is the indirect
    // count, which bounds vkCmdDrawIndirect. (The indexed vertex limit is
    // maxDrawIndexedIndexValue.)
    std::uint32_t maxDrawIndirectCount = 0;
    std::uint64_t heapSize = 0;  // bytes of device-local memory
    bool supportsGeometryShader = false;
    bool supportsSamplerAnisotropy = false;
    bool supportsSeparateDepthStencilLayouts = false;
    bool supportsTimelineSemaphore = false;
};

// "1.4.309" from VK_MAKE_API_VERSION.
std::string formatApiVersion(std::uint32_t version);

// "NVIDIA 551.23" from VK_MAKE_VERSION.
std::string formatDriverVersion(std::uint32_t version);

// The Vulkan loader's own version, independent of any device.
std::uint32_t loaderVersion();

// Is the loader present and usable at all?
bool loaderAvailable(std::string& error);

// Every physical device the loader finds, discrete GPUs first.
std::vector<Gpu> enumerateGpus(std::string& error);

// A short, human-readable summary of `gpus`, one line each.
std::string summarize(const std::vector<Gpu>& gpus);

}  // namespace omsi::render