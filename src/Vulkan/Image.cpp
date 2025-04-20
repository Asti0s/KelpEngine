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

Image::Image(const std::shared_ptr<Device>& device, const CreateInfo& createInfo) : m_device(device), m_createInfo(createInfo) {
    createImage();
    createImageView();
}

Image::Image(const std::shared_ptr<Device>& device, VkImage existingImage, const CreateInfo& createInfo, bool ownsImage) : m_device(device), m_createInfo(createInfo), m_image(existingImage), m_ownsImage(ownsImage) {
    createImageView();
}

Image::~Image() {
    cleanup();
}

Image::Image(Image&& other) noexcept : m_device(std::move(other.m_device)) , m_createInfo(other.m_createInfo) , m_image(other.m_image) , m_imageView(other.m_imageView) , m_allocation(other.m_allocation) {
    other.m_image = VK_NULL_HANDLE;
    other.m_imageView = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        cleanup();

        m_device = std::move(other.m_device);
        m_createInfo = other.m_createInfo;
        m_image = other.m_image;
        m_imageView = other.m_imageView;
        m_allocation = other.m_allocation;

        other.m_image = VK_NULL_HANDLE;
        other.m_imageView = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
    }
    return *this;
}

void Image::cleanup() {
    if (m_device) {
        if (m_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device->getHandle(), m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }
        if (m_image != VK_NULL_HANDLE && m_ownsImage) {
            vmaDestroyImage(m_device->getAllocator(), m_image, m_allocation);
            m_image = VK_NULL_HANDLE;
            m_allocation = VK_NULL_HANDLE;
        }
    }
}

void Image::createImage() {
    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = m_createInfo.type,
        .format = m_createInfo.format,
        .extent = m_createInfo.extent,
        .mipLevels = m_createInfo.mipLevels,
        .arrayLayers = m_createInfo.arrayLayers,
        .samples = m_createInfo.samples,
        .tiling = m_createInfo.tiling,
        .usage = m_createInfo.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = m_createInfo.initialLayout
    };

    const VmaAllocationCreateInfo allocationInfo{
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VK_CHECK(vmaCreateImage(m_device->getAllocator(), &imageInfo, &allocationInfo, &m_image, &m_allocation, nullptr));
}

std::unique_ptr<Image> Image::fromSwapchainImage(const std::shared_ptr<Device>& device, VkImage swapchainImage, const VkExtent3D& extent, VkFormat format) {
    const CreateInfo createInfo{
        .extent = extent,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .mipLevels = 1,
        .aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .arrayLayers = 1,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    return std::unique_ptr<Image>(new Image(device, swapchainImage, createInfo, false));
}

void Image::createImageView() {
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    switch (m_createInfo.type) {
        case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
        case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
        case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
        default: throw std::runtime_error("Unsupported image type");
    }

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image,
        .viewType = viewType,
        .format = m_createInfo.format,
        .subresourceRange = {
            .aspectMask = m_createInfo.aspectFlags,
            .baseMipLevel = 0,
            .levelCount = m_createInfo.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = m_createInfo.arrayLayers
        }
    };

    VK_CHECK(vkCreateImageView(m_device->getHandle(), &viewInfo, nullptr, &m_imageView));
}

void Image::cmdTransitionLayout(VkCommandBuffer commandBuffer, const Layout& oldLayout, const Layout& newLayout, uint32_t baseMipLevel, uint32_t levelCount) {
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = oldLayout.accessMask,
        .dstAccessMask = newLayout.accessMask,
        .oldLayout = oldLayout.layout,
        .newLayout = newLayout.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = m_image,
        .subresourceRange = {
            .aspectMask = m_createInfo.aspectFlags,
            .baseMipLevel = baseMipLevel,
            .levelCount = levelCount,
            .baseArrayLayer = 0,
            .layerCount = m_createInfo.arrayLayers
        }
    };

    vkCmdPipelineBarrier(commandBuffer, oldLayout.stageFlags, newLayout.stageFlags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool Image::supportsLinearBlitting() const {
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(m_device->getPhysicalDevice(), m_createInfo.format, &formatProps);
    return (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

void Image::cmdGenerateMipmaps(VkCommandBuffer commandBuffer, const Layout& finalLayout) {
    if (!supportsLinearBlitting())
        throw std::runtime_error("Image format does not support linear blitting");

    const Layout srcLayout{
        .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .accessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT
    };

    const Layout dstLayout{
        .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .accessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT
    };

    int32_t mipWidth = static_cast<int32_t>(m_createInfo.extent.width);
    int32_t mipHeight = static_cast<int32_t>(m_createInfo.extent.height);
    int32_t mipDepth = static_cast<int32_t>(m_createInfo.extent.depth);

    for (uint32_t i = 1; i < m_createInfo.mipLevels; i++) {
        cmdTransitionLayout(commandBuffer, dstLayout, srcLayout, i - 1, 1);

        const VkImageBlit blit{
            .srcSubresource = {
                .aspectMask = m_createInfo.aspectFlags,
                .mipLevel = i - 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .srcOffsets = {{0, 0, 0}, {mipWidth, mipHeight, mipDepth}},
            .dstSubresource = {
                .aspectMask = m_createInfo.aspectFlags,
                .mipLevel = i,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .dstOffsets = {
                {0, 0, 0},
                {
                    mipWidth > 1 ? mipWidth / 2 : 1,
                    mipHeight > 1 ? mipHeight / 2 : 1,
                    mipDepth > 1 ? mipDepth / 2 : 1
                }
            }
        };

        vkCmdBlitImage(commandBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        cmdTransitionLayout(commandBuffer, srcLayout, finalLayout, i - 1, 1);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
        if (mipDepth > 1) mipDepth /= 2;
    }

    cmdTransitionLayout(commandBuffer, dstLayout, finalLayout, m_createInfo.mipLevels - 1, 1);
}

void Image::cmdCopyFromBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer) {
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = m_createInfo.aspectFlags,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = m_createInfo.arrayLayers
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = m_createInfo.extent
    };

    vkCmdCopyBufferToImage(commandBuffer, buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void Image::cmdCopyFromImage(VkCommandBuffer commandBuffer, const Image& srcImage) {
    const VkImageCopy copyRegion {
        .srcSubresource = {
            .aspectMask = srcImage.m_createInfo.aspectFlags,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = srcImage.m_createInfo.arrayLayers
        },
        .srcOffset = {0, 0, 0},
        .dstSubresource = {
            .aspectMask = m_createInfo.aspectFlags,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = m_createInfo.arrayLayers
        },
        .dstOffset = {0, 0, 0},
        .extent = m_createInfo.extent
    };

    vkCmdCopyImage(commandBuffer, srcImage.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
}
