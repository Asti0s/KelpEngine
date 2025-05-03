#pragma once

#include "Viewer/Window.hpp"

#define VK_NO_PROTOTYPES
#include "vk_mem_alloc.h"
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>

class Device {
    public:
        enum QueueType : uint8_t {
            Graphics = 0,
            Transfer = 1,
            Compute = 2
        };


    public:
        Device(const std::shared_ptr<Window>& window);
        ~Device();

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        Device(Device&& other) = delete;
        Device& operator=(Device&& other) = delete;



        /**
        * @brief Waits for the device to finish executing all the commands.
        * Must be called before destroying the vulkan resources.
        */
        void waitIdle() const;

        /**
         * @brief Begins a single time command buffer for the given queue type.
         * /!\ Only one single time command buffer per queue type can be active at a time.
         *
         * @param queueType The queue type to use for the command buffer.
         * @return VkCommandBuffer The single time command buffer.
         */
        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands(QueueType queueType) const;
        void endSingleTimeCommands(QueueType queueType, VkCommandBuffer commandBuffer) const;


        /**
        * @brief Finds the memory type index that satisfies the given filter and properties.
        *
        * @param typeFilter The filter to apply to the memory type index.
        * @param properties The properties that the memory type index must satisfy.
        * @return uint32_t The memory type index that satisfies the given filter and properties.
        */
        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;


        /* Getters */
        [[nodiscard]] VkInstance        getInstance()                   const noexcept { return m_instance; };
        [[nodiscard]] VkPhysicalDevice  getPhysicalDevice()             const noexcept { return m_physicalDevice; };
        [[nodiscard]] VkDevice          getHandle()                     const noexcept { return m_device; };
        [[nodiscard]] VkSurfaceKHR      getSurface()                    const noexcept { return m_windowSurface; };
        [[nodiscard]] VkDescriptorPool  getDescriptorPool()             const noexcept { return m_descriptorPool; };
        [[nodiscard]] VmaAllocator      getAllocator()                  const noexcept { return m_allocator; };

        [[nodiscard]] VkCommandPool     getCommandPool(QueueType queueType)         const noexcept { return m_queueDatas[queueType].commandPool; };
        [[nodiscard]] uint32_t          getQueueFamilyIndex(QueueType queueType)    const noexcept { return m_queueDatas[queueType].queueFamilyIndex; };
        [[nodiscard]] VkQueue           getQueue(QueueType queueType)               const noexcept { return m_queueDatas[queueType].queue; };

        [[nodiscard]] const VkPhysicalDeviceMemoryProperties&   getMemoryProperties()           const noexcept { return m_memoryProperties; };
        [[nodiscard]] const VkPhysicalDeviceProperties&         getProperties()                 const noexcept { return m_properties; };
        [[nodiscard]] VkSampleCountFlagBits                     getMaxMsaaSamples()             const noexcept { return m_maxMsaaSamples; };



    private:
        struct QueueFamilyIndices {
            uint32_t graphicsFamily = std::numeric_limits<uint32_t>::max();
            uint32_t computeFamily = std::numeric_limits<uint32_t>::max();
            uint32_t transferFamily = std::numeric_limits<uint32_t>::max();
        };

        struct QueueDatas {
            uint32_t queueFamilyIndex = 0;
            VkQueue queue = VK_NULL_HANDLE;
            VkCommandPool commandPool = VK_NULL_HANDLE;
            VkCommandBuffer singleTimeCommandBuffer = VK_NULL_HANDLE;
        };


    private:
        void createInstance();
        void createSurface(const std::shared_ptr<Window>& window);
        void createDevice();
        void findMaxMsaaSamples();
        void createCommandPools();
        void createDescriptorPool();
        void createAllocator();
        void createSingleTimeCommandBuffers();

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
        VkFence m_singleTimeCommandsFence{};

        VkPhysicalDeviceMemoryProperties m_memoryProperties{};
        VkPhysicalDeviceProperties m_properties{};
        VkSampleCountFlagBits m_maxMsaaSamples = VK_SAMPLE_COUNT_1_BIT;

        std::array<QueueDatas, 3> m_queueDatas;
};
