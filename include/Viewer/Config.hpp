#pragma once

#define VK_NO_PROTOTYPES
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>

namespace Config {

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    static constexpr float CAMERA_SPEED = 10;
    static constexpr float CAMERA_SENSITIVITY = 0.1;

    static constexpr std::array<const char *const, 1> REQUIRED_VALIDATION_LAYERS = {
        "VK_LAYER_KHRONOS_validation"
    };

    static constexpr std::array<const char *const, 6> REQUIRED_DEVICE_EXTENSIONS = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME
    };

    static constexpr std::array<const char *, 1> REQUIRED_INSTANCE_EXTENSIONS = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    };

}   // namespace Config
