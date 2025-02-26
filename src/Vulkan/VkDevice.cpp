#include "Vulkan/VkDevice.hpp"

#define VMA_IMPLEMENTATION
#include "Config.hpp"
#include "Vulkan/VkUtils.hpp"
#include "Window.hpp"

#include "GLFW/glfw3.h"
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Vk;

Device::Device(const std::shared_ptr<Window>& window) {
    VK_CHECK(volkInitialize());

    createInstance();
    createSurface(window);
    findPhysicalDevice();
    createDevice();
    createCommandPools();
    createDescriptorPool();
    createSingleTimeCommandBuffers();
    findMaxMsaaSamples();
    createAllocator();

    std::cout << "Graphic: " << m_queueDatas[Graphics].queueFamilyIndex << " " << m_queueDatas[Graphics].queue << " " << m_queueDatas[Graphics].commandPool << std::endl;
    std::cout << "Transfer: " << m_queueDatas[Transfer].queueFamilyIndex << " " << m_queueDatas[Transfer].queue << " " << m_queueDatas[Transfer].commandPool << std::endl;
    std::cout << "Compute: " << m_queueDatas[Compute].queueFamilyIndex << " " << m_queueDatas[Compute].queue << " " << m_queueDatas[Compute].commandPool << std::endl;
}

Device::~Device() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        vmaDestroyAllocator(m_allocator);
        vkDestroyDescriptorPool(m_device, m_descriptorPool, VK_NULL_HANDLE);

        vkDestroyCommandPool(m_device, m_queueDatas[Graphics].commandPool, VK_NULL_HANDLE);
        if (m_queueDatas[Transfer].queueFamilyIndex != m_queueDatas[Graphics].queueFamilyIndex)
            vkDestroyCommandPool(m_device, m_queueDatas[Transfer].commandPool, VK_NULL_HANDLE);
        if (m_queueDatas[Compute].queueFamilyIndex != m_queueDatas[Graphics].queueFamilyIndex)
            vkDestroyCommandPool(m_device, m_queueDatas[Compute].commandPool, VK_NULL_HANDLE);

        vkDestroyFence(m_device, m_singleTimeCommandsFence, VK_NULL_HANDLE);
        vkDestroyDevice(m_device, VK_NULL_HANDLE);
        vkDestroySurfaceKHR(m_instance, m_windowSurface, VK_NULL_HANDLE);
        vkDestroyInstance(m_instance, VK_NULL_HANDLE);
    }
}

void Device::endSingleTimeCommands(QueueType queueType, VkCommandBuffer commandBuffer) const {
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    VK_CHECK(vkQueueSubmit(m_queueDatas[queueType].queue, 1, &submitInfo, m_singleTimeCommandsFence));

    VK_CHECK(vkWaitForFences(m_device, 1, &m_singleTimeCommandsFence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
    VK_CHECK(vkResetFences(m_device, 1, &m_singleTimeCommandsFence));
}

VkCommandBuffer Device::beginSingleTimeCommands(QueueType queueType) const {
    VkCommandBuffer commandBuffer = m_queueDatas[queueType].singleTimeCommandBuffer;

    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    return commandBuffer;
}

void Device::createSingleTimeCommandBuffers() {
    // Command buffer allocations
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    allocInfo.commandPool = m_queueDatas[Graphics].commandPool;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &m_queueDatas[Graphics].singleTimeCommandBuffer));

    if (m_queueDatas[Transfer].queueFamilyIndex != m_queueDatas[Graphics].queueFamilyIndex) {
        allocInfo.commandPool = m_queueDatas[Transfer].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &m_queueDatas[Transfer].singleTimeCommandBuffer));
    } else {
        m_queueDatas[Transfer].singleTimeCommandBuffer = m_queueDatas[Graphics].singleTimeCommandBuffer;
    }

    if (m_queueDatas[Compute].queueFamilyIndex != m_queueDatas[Graphics].queueFamilyIndex && m_queueDatas[Compute].queueFamilyIndex != m_queueDatas[Transfer].queueFamilyIndex) {
        allocInfo.commandPool = m_queueDatas[Compute].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &m_queueDatas[Compute].singleTimeCommandBuffer));
    } else {
        m_queueDatas[Compute].singleTimeCommandBuffer = m_queueDatas[Graphics].singleTimeCommandBuffer;
    }


    // Fence creation
    const VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateFence(m_device, &fenceInfo, VK_NULL_HANDLE, &m_singleTimeCommandsFence));
}

void Device::createAllocator() {
    const VmaVulkanFunctions vma_vulkan_func{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
        .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
        .vkAllocateMemory = vkAllocateMemory,
        .vkFreeMemory = vkFreeMemory,
        .vkMapMemory = vkMapMemory,
        .vkUnmapMemory = vkUnmapMemory,
        .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
        .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
        .vkBindBufferMemory = vkBindBufferMemory,
        .vkBindImageMemory = vkBindImageMemory,
        .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
        .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
        .vkCreateBuffer = vkCreateBuffer,
        .vkDestroyBuffer = vkDestroyBuffer,
        .vkCreateImage = vkCreateImage,
        .vkDestroyImage = vkDestroyImage,
        .vkCmdCopyBuffer = vkCmdCopyBuffer,
        .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR,
        .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR,
        .vkBindBufferMemory2KHR = vkBindBufferMemory2KHR,
        .vkBindImageMemory2KHR = vkBindImageMemory2KHR,
        .vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR,
        .vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
        .vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements
    };

    const VmaAllocatorCreateInfo allocatorInfo = {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = m_physicalDevice,
        .device = m_device,
        .pVulkanFunctions = &vma_vulkan_func,
        .instance = m_instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator));
}

void Device::createDescriptorPool() {
    constexpr std::array<VkDescriptorPoolSize, 11> descriptorPoolSizes = {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    const VkDescriptorPoolCreateInfo poolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1000,
        .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
        .pPoolSizes = descriptorPoolSizes.data()
    };

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolCreateInfo, nullptr, &m_descriptorPool));
}

void Device::createCommandPools() {
    VkCommandPoolCreateInfo commandPoolCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };

    commandPoolCreateInfo.queueFamilyIndex = m_queueDatas[Graphics].queueFamilyIndex;
    VK_CHECK(vkCreateCommandPool(m_device, &commandPoolCreateInfo, VK_NULL_HANDLE, &m_queueDatas[Graphics].commandPool));

    if (m_queueDatas[Transfer].queueFamilyIndex != m_queueDatas[Graphics].queueFamilyIndex) {
        commandPoolCreateInfo.queueFamilyIndex = m_queueDatas[Transfer].queueFamilyIndex;
        VK_CHECK(vkCreateCommandPool(m_device, &commandPoolCreateInfo, VK_NULL_HANDLE, &m_queueDatas[Transfer].commandPool));
    } else {
        m_queueDatas[Transfer].commandPool = m_queueDatas[Graphics].commandPool;
    }

    if (m_queueDatas[Compute].queueFamilyIndex != m_queueDatas[Graphics].queueFamilyIndex && m_queueDatas[Compute].queueFamilyIndex != m_queueDatas[Transfer].queueFamilyIndex) {
        commandPoolCreateInfo.queueFamilyIndex = m_queueDatas[Compute].queueFamilyIndex;
        VK_CHECK(vkCreateCommandPool(m_device, &commandPoolCreateInfo, VK_NULL_HANDLE, &m_queueDatas[Compute].commandPool));
    } else {
        m_queueDatas[Compute].commandPool = m_queueDatas[Graphics].commandPool;
    }
}

void Device::findMaxMsaaSamples() {
    const VkSampleCountFlags counts = m_properties.limits.framebufferColorSampleCounts & m_properties.limits.framebufferDepthSampleCounts;

    if ((counts & VK_SAMPLE_COUNT_64_BIT) != 0U)
        m_maxMsaaSamples = VK_SAMPLE_COUNT_64_BIT;
    else if ((counts & VK_SAMPLE_COUNT_32_BIT) != 0U)
        m_maxMsaaSamples = VK_SAMPLE_COUNT_32_BIT;
    else if ((counts & VK_SAMPLE_COUNT_16_BIT) != 0U)
        m_maxMsaaSamples = VK_SAMPLE_COUNT_16_BIT;
    else if ((counts & VK_SAMPLE_COUNT_8_BIT) != 0U)
        m_maxMsaaSamples = VK_SAMPLE_COUNT_8_BIT;
    else if ((counts & VK_SAMPLE_COUNT_4_BIT) != 0U)
        m_maxMsaaSamples = VK_SAMPLE_COUNT_4_BIT;
    else if ((counts & VK_SAMPLE_COUNT_2_BIT) != 0U)
        m_maxMsaaSamples = VK_SAMPLE_COUNT_2_BIT;
    else
        m_maxMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
}

bool Device::checkForRequiredFeatures(VkPhysicalDevice device) {
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
    };

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext = &accelerationStructureFeatures,
    };

    VkPhysicalDeviceVulkan12Features vulkan12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &rayTracingPipelineFeatures,
        .scalarBlockLayout = VK_TRUE,
    };

    VkPhysicalDeviceVulkan13Features vulkan13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan12Features,
    };

    VkPhysicalDeviceFeatures2 deviceFeatures2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13Features,
    };

    vkGetPhysicalDeviceFeatures2(device, &deviceFeatures2);

    return static_cast<bool>(vulkan12Features.bufferDeviceAddress)
        && static_cast<bool>(vulkan12Features.descriptorBindingPartiallyBound)
        && static_cast<bool>(vulkan12Features.descriptorBindingSampledImageUpdateAfterBind)
        && static_cast<bool>(vulkan12Features.descriptorBindingStorageImageUpdateAfterBind)
        && static_cast<bool>(vulkan12Features.shaderSampledImageArrayNonUniformIndexing)
        && static_cast<bool>(vulkan12Features.runtimeDescriptorArray)
        && static_cast<bool>(vulkan12Features.scalarBlockLayout)
        && static_cast<bool>(vulkan13Features.dynamicRendering)
        && static_cast<bool>(deviceFeatures2.features.samplerAnisotropy)
        && static_cast<bool>(accelerationStructureFeatures.accelerationStructure)
        && static_cast<bool>(accelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind)
        && static_cast<bool>(rayTracingPipelineFeatures.rayTracingPipeline);
}

bool Device::checkForRequiredExtensions(VkPhysicalDevice device) {
    uint32_t extensionCount = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(device, VK_NULL_HANDLE, &extensionCount, VK_NULL_HANDLE));

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(device, VK_NULL_HANDLE, &extensionCount, availableExtensions.data()));

    for (const char* requiredExtension : REQUIRED_DEVICE_EXTENSIONS) {
        bool found = false;
        for (const VkExtensionProperties& extension : availableExtensions) {
            if (strcmp(extension.extensionName, requiredExtension) == 0) {
                found = true;
                break;
            }
        }

        if (!found)
            return false;
    }

    return true;
}

bool Device::findQueueFamilies(VkPhysicalDevice device, QueueFamilyIndices& indices) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, VK_NULL_HANDLE);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());


    // Search the graphics queue family
    for (size_t i = 0; i < queueFamilies.size(); i++) {
        if (static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)) {

            VkBool32 presentSupport = VK_FALSE;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_windowSurface, &presentSupport));

            if (presentSupport == VK_TRUE) {
                indices.graphicsFamily = static_cast<int>(i);
                indices.computeFamily = static_cast<int>(i);
                indices.transferFamily = static_cast<int>(i);
                break;
            }
        }
    }

    if (indices.graphicsFamily == std::numeric_limits<uint32_t>::max())
        return false;


    // Search the async transfer queue family
    for (size_t i = 0; i < queueFamilies.size(); i++) {
        if (static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
           !static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
           !static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {

            indices.transferFamily = static_cast<int>(i);
            break;
        }
    }


    // Search the async compute queue family
    for (size_t i = 0; i < queueFamilies.size(); i++) {
        if (static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
           !static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            static_cast<bool>(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {

            indices.computeFamily = static_cast<int>(i);
            break;
        }
    }

    return true;
}

bool Device::checkForDeviceSuitability(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    if (!findQueueFamilies(device, indices))
        return false;

    if (!checkForRequiredFeatures(device))
        return false;

    if (!checkForRequiredExtensions(device))
        return false;

    return true;
}

void Device::findPhysicalDevice() {
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &deviceCount, VK_NULL_HANDLE));

    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data()));

    for (const VkPhysicalDevice& device : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);

        if (!checkForDeviceSuitability(device)) {
            std::cout << "Device not suitable: \"" << properties.deviceName << "\":" << std::endl;
            continue;
        }

        std::cout << "Suitable device found: \"" << properties.deviceName << "\":" << std::endl;
        m_physicalDevice = device;
    }

    if (m_physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("Failed to find a suitable GPU. (Tests failed)");
}

void Device::createDevice() {
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);


    // Required features
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .accelerationStructure = VK_TRUE,
        .descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE,
    };

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext = &accelerationStructureFeatures,
        .rayTracingPipeline = VK_TRUE,
    };

    VkPhysicalDeviceVulkan12Features vulkan12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &rayTracingPipelineFeatures,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .scalarBlockLayout = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };

    VkPhysicalDeviceVulkan13Features vulkan13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan12Features,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 deviceFeatures2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13Features,
        .features = { .samplerAnisotropy = VK_TRUE }
    };


    // Set queue family indices
    QueueFamilyIndices indices;
    findQueueFamilies(m_physicalDevice, indices);

    m_queueDatas[Graphics].queueFamilyIndex = indices.graphicsFamily;
    m_queueDatas[Transfer].queueFamilyIndex = indices.transferFamily;
    m_queueDatas[Compute].queueFamilyIndex = indices.computeFamily;


    // Queues create infos
    constexpr float queuePriority = 1.0;

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.push_back({
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = indices.graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    });

    if (indices.transferFamily != indices.graphicsFamily) {
        queueCreateInfos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = indices.transferFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
        });
    }

    if (indices.computeFamily != indices.graphicsFamily) {
        queueCreateInfos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = indices.computeFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
        });
    }


    // Device creation
    const VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &deviceFeatures2,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledLayerCount = static_cast<uint32_t>(REQUIRED_VALIDATION_LAYERS.size()),
        .ppEnabledLayerNames = REQUIRED_VALIDATION_LAYERS.empty() ? nullptr : REQUIRED_VALIDATION_LAYERS.data(),
        .enabledExtensionCount = static_cast<uint32_t>(REQUIRED_DEVICE_EXTENSIONS.size()),
        .ppEnabledExtensionNames = REQUIRED_DEVICE_EXTENSIONS.data(),
    };

    VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceCreateInfo, VK_NULL_HANDLE, &m_device));
    volkLoadDevice(m_device);


    // Get queues
    vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_queueDatas[Graphics].queue);

    if (indices.transferFamily != indices.graphicsFamily)
        vkGetDeviceQueue(m_device, indices.transferFamily, 0, &m_queueDatas[Transfer].queue);
    else
        m_queueDatas[Transfer].queue = m_queueDatas[Graphics].queue;

    if (indices.computeFamily != indices.graphicsFamily)
        vkGetDeviceQueue(m_device, indices.computeFamily, 0, &m_queueDatas[Compute].queue);
    else
        m_queueDatas[Compute].queue = m_queueDatas[Graphics].queue;
}

void Device::createSurface(const std::shared_ptr<Window>& window) {
    VK_CHECK(glfwCreateWindowSurface(m_instance, window->getHandle(), VK_NULL_HANDLE, &m_windowSurface));
}

void Device::createInstance() {
    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr)
        throw std::runtime_error("Failed to get required instance extensions.");

    std::vector<const char *> instanceExtensions(REQUIRED_INSTANCE_EXTENSIONS.begin(), REQUIRED_INSTANCE_EXTENSIONS.end());
    instanceExtensions.reserve(extensionCount + instanceExtensions.size());
    for (uint32_t i = 0; i < extensionCount; i++)
        instanceExtensions.emplace_back(extensions[i]);

    constexpr VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "No name",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    const VkInstanceCreateInfo instanceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(REQUIRED_VALIDATION_LAYERS.size()),
        .ppEnabledLayerNames = REQUIRED_VALIDATION_LAYERS.empty() ? nullptr : REQUIRED_VALIDATION_LAYERS.data(),
        .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
        .ppEnabledExtensionNames = instanceExtensions.data()
    };

    VK_CHECK(vkCreateInstance(&instanceCreateInfo, VK_NULL_HANDLE, &m_instance));
    volkLoadInstance(m_instance);
}

void Device::waitIdle() const {
    VK_CHECK(vkDeviceWaitIdle(m_device));
}

uint32_t Device::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++)
        if (((typeFilter & (1 << i)) != 0) && (m_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;

    throw std::runtime_error("Failed to find suitable memory type.");
}
