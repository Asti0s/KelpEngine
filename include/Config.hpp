#pragma once

#define VK_NO_PROTOTYPES
#include "volk.h"

#include <array>

#define MAX_FRAMES_IN_FLIGHT 3

static constexpr std::array<const char *const, 1> REQUIRED_VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation"
};

static constexpr std::array<const char *const, 1> REQUIRED_DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static constexpr std::array<const char *, 1> REQUIRED_INSTANCE_EXTENSIONS = {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME
};
