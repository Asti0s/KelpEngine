#pragma once

#include "Window.hpp"

#define VK_NO_PROTOTYPES
#include "vk_mem_alloc.h"
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

class Device {
    public:
        Device(const std::shared_ptr<Window>& window);
        ~Device();

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        Device(Device&& other) = delete;
        Device& operator=(Device&& other) = delete;


        /**
        * @brief Finds the memory type index that satisfies the given filter and properties.
        *
        * @param typeFilter The filter to apply to the memory type index.
        * @param properties The properties that the memory type index must satisfy.
        * @return uint32_t The memory type index that satisfies the given filter and properties.
        */
        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        /**
        * @brief Waits for the device to finish executing all the commands.
        * Must be called before destroying the vulkan resources.
        */
        void waitIdle() const;


        /* Getters */
        [[nodiscard]] VkInstance        getInstance()                   const noexcept { return m_instance; };
        [[nodiscard]] VkPhysicalDevice  getPhysicalDevice()             const noexcept { return m_physicalDevice; };
        [[nodiscard]] VkDevice          getHandle()                     const noexcept { return m_device; };
        [[nodiscard]] VkSurfaceKHR      getSurface()                    const noexcept { return m_windowSurface; };
        [[nodiscard]] VkDescriptorPool  getDescriptorPool()             const noexcept { return m_descriptorPool; };
        [[nodiscard]] VmaAllocator      getAllocator()                  const noexcept { return m_allocator; };

        [[nodiscard]] VkCommandPool     getGraphicsCommandPool()         const noexcept { return m_graphicsQueueData.commandPool; };
        [[nodiscard]] uint32_t          getGraphicsQueueFamilyIndex()    const noexcept { return m_graphicsQueueData.queueFamilyIndex; };
        [[nodiscard]] VkQueue           getGraphicsQueue()               const noexcept { return m_graphicsQueueData.queue; };

        [[nodiscard]] VkCommandPool     getTransferCommandPool()        const noexcept { return m_transferQueueData.commandPool; };
        [[nodiscard]] uint32_t          getTransferQueueFamilyIndex()   const noexcept { return m_transferQueueData.queueFamilyIndex; };
        [[nodiscard]] VkQueue           getTransferQueue()              const noexcept { return m_transferQueueData.queue; };

        [[nodiscard]] VkCommandPool     getComputeCommandPool()         const noexcept { return m_computeQueueData.commandPool; };
        [[nodiscard]] uint32_t          getComputeQueueFamilyIndex()    const noexcept { return m_computeQueueData.queueFamilyIndex; };
        [[nodiscard]] VkQueue           getComputeQueue()               const noexcept { return m_computeQueueData.queue; };

        [[nodiscard]] const VkPhysicalDeviceMemoryProperties&   getMemoryProperties()           const noexcept { return m_memoryProperties; };
        [[nodiscard]] const VkPhysicalDeviceProperties&         getProperties()                 const noexcept { return m_properties; };
        [[nodiscard]] VkSampleCountFlagBits                     getMaxMsaaSamples()             const noexcept { return m_maxMsaaSamples; };



    private:
        static constexpr std::array<const char *const, 1> m_validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        static constexpr std::array<const char *const, 1> m_deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        std::vector<const char *> m_instanceExtensions = {
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        };

        struct QueueFamilyIndices {
            uint32_t graphicsFamily = std::numeric_limits<uint32_t>::max();
            uint32_t computeFamily = std::numeric_limits<uint32_t>::max();
            uint32_t transferFamily = std::numeric_limits<uint32_t>::max();
        };

        struct QueueDatas {
            uint32_t queueFamilyIndex = 0;
            VkQueue queue = VK_NULL_HANDLE;
            VkCommandPool commandPool = VK_NULL_HANDLE;
        };


    private:
        void createInstance();
        void createSurface(const std::shared_ptr<Window>& window);
        void createDevice();
        void findMaxMsaaSamples();
        void createCommandPools();
        void createDescriptorPool();
        void createAllocator();

        void findPhysicalDevice();
        bool findQueueFamilies(VkPhysicalDevice device, QueueFamilyIndices& indices);
        static bool checkForRequiredFeatures(VkPhysicalDevice device);
        static bool checkForRequiredExtensions(VkPhysicalDevice device);
        bool checkForDeviceSuitability(VkPhysicalDevice device);


    private:
        VkInstance m_instance{};
        VkPhysicalDevice m_physicalDevice{};
        VkDevice m_device{};
        VkSurfaceKHR m_windowSurface{};
        VkDescriptorPool m_descriptorPool{};
        VmaAllocator m_allocator{};

        VkPhysicalDeviceMemoryProperties m_memoryProperties{};
        VkPhysicalDeviceProperties m_properties{};
        VkSampleCountFlagBits m_maxMsaaSamples = VK_SAMPLE_COUNT_1_BIT;

        QueueDatas m_graphicsQueueData;
        QueueDatas m_transferQueueData;
        QueueDatas m_computeQueueData;
};
