// SPDX-License-Identifier: MIT
//
// evigit-vk - what this machine's Vulkan driver can do.
//
// Run it before writing any renderer: the renderer has to target what the driver supports,
// not what the specification allows, and the answer differs per GPU. See
// src/render/include/omsi/render/device.hpp for why that matters here.

#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

#include "omsi/core/log.hpp"
#include "omsi/render/device.hpp"

int main() {
    try {
        omsi::core::setConsoleLevel(omsi::core::LogLevel::Info);

        std::string error;
        if (!omsi::render::loaderAvailable(error)) {
            std::cerr << "Vulkan is not usable on this machine.\n  " << error << '\n';
            return 2;
        }

        std::cout << "Loader API version: "
                  << omsi::render::formatApiVersion(omsi::render::loaderVersion()) << "\n\n";

        const auto gpus = omsi::render::enumerateGpus(error);
        if (gpus.empty()) {
            std::cerr << "No Vulkan device found.\n  " << error << '\n';
            return 2;
        }

        std::cout << "Devices:\n" << omsi::render::summarize(gpus);

        for (const auto& gpu : gpus) {
            std::cout << "\n" << gpu.name << "\n"
                      << "  vendor id ........ " << gpu.vendorId << "\n"
                      << "  device id ........ " << gpu.deviceId << "\n"
                      << "  max texture 2d ... " << gpu.maxTextureDimension2D << "\n"
                      << "  max draw indirect " << gpu.maxDrawIndirectCount << "\n"
                      << "  uniform range .... " << (gpu.maxUniformBufferRange / 1024) << " KB\n"
                      << "  push constants ... " << gpu.maxPushConstantsSize << " B\n"
                      << "  anisotropy ....... " << (gpu.supportsSamplerAnisotropy ? "yes" : "no")
                      << "\n"
                      << "  geometry shader .. " << (gpu.supportsGeometryShader ? "yes" : "no")
                      << "\n"
                      << "  timeline sem .... " << (gpu.supportsTimelineSemaphore ? "yes" : "no")
                      << "\n"
                      << "  separate depth .. "
                      << (gpu.supportsSeparateDepthStencilLayouts ? "yes" : "no") << "\n";
        }
        std::cout << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "evigit-vk: " << e.what() << '\n';
        return 1;
    }
}