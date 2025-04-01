#include "Vulkan/Buffer.hpp"

#include "Vulkan/Device.hpp"
#include "Vulkan/Utils.hpp"

#include "vk_mem_alloc.h"
#include <vulkan/vulkan_core.h>

#include <cstdlib>
#include <memory>
#include <utility>

Buffer::Buffer(const std::shared_ptr<Device>& device, size_t size, VkBufferUsageFlags bufferUsage, VmaAllocationCreateFlags allocationFlags) : m_device(device) {
    // Buffer creation
    const VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = bufferUsage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    const VmaAllocationCreateInfo allocationInfo = {
        .flags = allocationFlags,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VK_CHECK(vmaCreateBuffer(m_device->getAllocator(), &bufferCreateInfo, &allocationInfo, &m_buffer, &m_allocation, nullptr));


    // Device address
    if (static_cast<bool>(bufferUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) {
        const VkBufferDeviceAddressInfo bufferDeviceAddressInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = m_buffer
        };

        m_deviceAddress = vkGetBufferDeviceAddress(m_device->getHandle(), &bufferDeviceAddressInfo);
    }
}

Buffer::~Buffer() {
    cleanup();
}

Buffer::Buffer(Buffer&& other) noexcept : m_device(std::move(other.m_device)), m_deviceAddress(other.m_deviceAddress), m_allocation(other.m_allocation), m_buffer(other.m_buffer) {
    other.m_deviceAddress = 0;
    other.m_allocation = VK_NULL_HANDLE;
    other.m_buffer = VK_NULL_HANDLE;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = std::move(other.m_device);
        m_deviceAddress = other.m_deviceAddress;
        m_allocation = other.m_allocation;
        m_buffer = other.m_buffer;

        other.m_deviceAddress = 0;
        other.m_allocation = VK_NULL_HANDLE;
        other.m_buffer = VK_NULL_HANDLE;
    }

    return *this;
}

void Buffer::cleanup() {
    if (m_buffer != nullptr)
        vmaDestroyBuffer(m_device->getAllocator(), m_buffer, m_allocation);
}

void Buffer::map(void **data) const {
    VK_CHECK(vmaMapMemory(m_device->getAllocator(), m_allocation, data));
}

void Buffer::unmap() const noexcept {
    vmaUnmapMemory(m_device->getAllocator(), m_allocation);
}

void Buffer::copyFrom(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkDeviceSize size) const {
    const VkBufferCopy bufferCopy = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size
    };

    vkCmdCopyBuffer(commandBuffer, srcBuffer, m_buffer, 1, &bufferCopy);
}
