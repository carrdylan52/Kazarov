#pragma once

// VK_KHR_portability_subset, which Vulkan-on-Metal drivers expose, is declared as a beta extension.
#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gfx::detail {

// What device selection looks at, gathered once per device so the decision is a pure function.
struct DeviceCaps {
    uint32_t                                       api_version = 0;
    VkPhysicalDeviceFeatures                       features = {};
    VkPhysicalDeviceVulkan12Features               features12 = {};
    VkPhysicalDeviceVulkan13Features               features13 = {};
    VkPhysicalDeviceVulkan12Properties             properties12 = {};
    VkPhysicalDeviceLimits                         limits = {};
    std::vector<std::string>                       extensions;
    bool                                           portability_subset = false;
    VkPhysicalDevicePortabilitySubsetFeaturesKHR   portability = {};
    VkPhysicalDevicePortabilitySubsetPropertiesKHR portability_properties = {};
    // A D32_SFLOAT_S8_UINT or D24_UNORM_S8_UINT depth/stencil attachment that copies both ways.
    bool depth_stencil_format = false;
    // A graphics queue family, that can also present to the window unless the renderer is offscreen.
    bool graphics_queue = false;
};

DeviceCaps QueryDeviceCaps(VkPhysicalDevice device, VkSurfaceKHR surface);
bool       HasExtension(const DeviceCaps &caps, const char *name);
// Every requirement the device fails, in words; empty when the renderer can use it. Nothing the renderer
// calls was introduced by 1.4, so min_api_version, the game's floor, is 1.3 or 1.4 by the game's choice.
std::vector<std::string> MissingRequirements(const DeviceCaps &caps, bool offscreen, uint32_t min_api_version);
std::string              VersionName(uint32_t version);
// The apiVersion the instance asks for: the game's floor. A 1.0 loader refuses anything higher, and
// a higher one would claim the renderer may use what a 1.3 device does not have.
uint32_t                 InstanceApiVersion(uint32_t min_api_version);
// Portability-subset features the renderer works around (it does not need them), for the log.
std::vector<std::string> PortabilityWorkarounds(const DeviceCaps &caps);

} // namespace gfx::detail
