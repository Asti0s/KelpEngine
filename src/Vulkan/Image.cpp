#include "Vulkan/Image.hpp"

#include "Vulkan/Device.hpp"
#include "Vulkan/Utils.hpp"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

Image::Image(const std::shared_ptr<Device>& device, const VkExtent3D& extent, VkImageUsageFlags usage, VkFormat format, VkImageType type, uint8_t mipLevels, VkImageAspectFlags aspectFlags) : m_device(device), m_extent(extent), m_format(format), m_mipLevels(mipLevels), m_aspectFlags(aspectFlags) {
    // Image creation
    const VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = type,
        .format = format,
        .extent = extent,
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    constexpr VmaAllocationCreateInfo allocationInfo = {
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VK_CHECK(vmaCreateImage(m_device->getAllocator(), &imageInfo, &allocationInfo, &m_image, &m_allocation, nullptr));


    // Image view creation
    const VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image,
        .viewType =
            type == VK_IMAGE_TYPE_1D ? VK_IMAGE_VIEW_TYPE_1D :
            type == VK_IMAGE_TYPE_2D ? VK_IMAGE_VIEW_TYPE_2D : // NOLINT(readability-avoid-nested-conditional-operator)
            VK_IMAGE_VIEW_TYPE_3D,
        .format = format,
        .subresourceRange = {
            .aspectMask = m_aspectFlags,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VK_CHECK(vkCreateImageView(m_device->getHandle(), &viewInfo, nullptr, &m_imageView));
}

Image::~Image() {
    cleanup();
}

Image::Image(Image&& other) noexcept : m_device(std::move(other.m_device)), m_image(other.m_image), m_imageView(other.m_imageView), m_allocation(other.m_allocation), m_extent(other.m_extent), m_format(other.m_format), m_mipLevels(other.m_mipLevels), m_aspectFlags(other.m_aspectFlags) {
    other.m_image = VK_NULL_HANDLE;
    other.m_imageView = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = std::move(other.m_device);
        m_image = other.m_image;
        m_imageView = other.m_imageView;
        m_allocation = other.m_allocation;
        m_extent = other.m_extent;
        m_format = other.m_format;
        m_mipLevels = other.m_mipLevels;
        m_aspectFlags = other.m_aspectFlags;

        other.m_image = VK_NULL_HANDLE;
        other.m_imageView = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
    }

    return *this;
}

void Image::cleanup() {
    if (m_imageView != VK_NULL_HANDLE)
        vkDestroyImageView(m_device->getHandle(), m_imageView, nullptr);
    if (m_image != VK_NULL_HANDLE)
        vmaDestroyImage(m_device->getAllocator(), m_image, m_allocation);
}

void Image::cmdCopyFromBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer) {
    const VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = m_aspectFlags,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = m_extent
    };

    vkCmdCopyBufferToImage(commandBuffer, buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void Image::cmdGenerateMipmaps(VkCommandBuffer commandBuffer) {
    int mipWidth = static_cast<int>(m_extent.width);
    int mipHeight = static_cast<int>(m_extent.height);
    int mipDepth = static_cast<int>(m_extent.depth);


    // Check if the format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(m_device->getPhysicalDevice(), m_format, &formatProperties);
    if (!static_cast<bool>(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        throw std::runtime_error("Texture image format does not support linear blitting");


    // Transition to transfer dst
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = m_image,
        .subresourceRange = {
            .aspectMask = m_aspectFlags,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };


    // Loop through all mip levels and generate them
    for (uint8_t i = 1; i < m_mipLevels; i++) {
        // Wait for the previous mip level to be filled and transition it to transfer source layout
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);


        // Blit from previous mip level to current mip level
        const VkImageBlit blit = {
            .srcSubresource = {
                .aspectMask = m_aspectFlags,
                .mipLevel = i - 1U,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .srcOffsets = {
                {0, 0, 0},
                {mipWidth, mipHeight, mipDepth}
            },
            .dstSubresource = {
                .aspectMask = m_aspectFlags,
                .mipLevel = i,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .dstOffsets = {
                {0, 0, 0},
                {
                    .x = mipWidth > 1 ? mipWidth / 2 : 1,
                    .y = mipHeight > 1 ? mipHeight / 2 : 1,
                    .z = mipDepth > 1 ? mipDepth / 2 : 1
                }
            }
        };

        vkCmdBlitImage(commandBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);


        // Wait for the blit to finish and transition the previous mip level to final layout
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);


        // Update mip dimensions
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
        if (mipDepth > 1) mipDepth /= 2;
    }


    // Transition the last mip level to final layout
    barrier.subresourceRange.baseMipLevel = m_mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
