#pragma once

#include "Device.hpp"

#include "vk_mem_alloc.h"
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdlib>
#include <memory>

class Buffer {
    public:
        Buffer(const std::shared_ptr<Device>& device, size_t size, VkBufferUsageFlags bufferUsage, VmaAllocationCreateFlags allocationFlags = 0, VkDeviceSize alignment = 0);
        ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;


        /**
        * @brief Give to the host access to the buffer memory via a pointer
        *
        * @param data (return) pointer to the buffer memory
        */
        void map(void **data) const;

        /**
        * @brief Unmap the buffer memory
        */
        void unmap() const noexcept;

        /**
        * @brief Copy data from a buffer to another
        *
        * @param commandBuffer command buffer to record the copy command
        * @param srcBuffer source buffer
        * @param size size of the data to copy
        */
        void copyFrom(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkDeviceSize size) const;


        /* Getters */
        [[nodiscard]] VkBuffer          getHandle()         const noexcept { return m_buffer; };
        [[nodiscard]] VmaAllocation     getAllocation()     const noexcept { return m_allocation; };
        [[nodiscard]] VkDeviceAddress   getDeviceAddress()  const noexcept { return m_deviceAddress; };


    private:
        void cleanup();


    private:
        std::shared_ptr<Device> m_device;

        VkDeviceAddress m_deviceAddress{};
        VmaAllocation m_allocation{};
        VkBuffer m_buffer{};
};
