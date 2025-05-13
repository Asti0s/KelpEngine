#pragma once

#include "Device.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>

class Image {
    public:
        struct CreateInfo {
            VkExtent3D extent{};
            VkImageUsageFlags usage{};
            VkFormat format{};
            VkImageType type = VK_IMAGE_TYPE_2D;
            uint8_t mipLevels = 1;
            VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            uint32_t arrayLayers = 1;
            VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
            VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        };

        struct Layout {
            VkImageLayout layout;
            VkAccessFlags accessMask;
            VkPipelineStageFlags stageFlags;
        };

        struct SwapchainImageInfo {
            VkImage image;
            VkExtent3D extent;
            VkFormat format;
        };

    public:
        explicit Image(const std::shared_ptr<Device>& device, const CreateInfo& createInfo);
        static std::unique_ptr<Image> fromSwapchainImage(const std::shared_ptr<Device>& device, VkImage swapchainImage, const VkExtent3D& extent, VkFormat format);
        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
        Image(Image&& other) noexcept;
        Image& operator=(Image&& other) noexcept;

        void cmdTransitionLayout(VkCommandBuffer commandBuffer, const Layout& oldLayout, const Layout& newLayout, uint32_t baseMipLevel = 0, uint32_t levelCount = VK_REMAINING_MIP_LEVELS);
        void cmdCopyFromBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, const VkExtent3D& extent, uint32_t mipLevel = 0);
        void cmdGenerateMipmaps(VkCommandBuffer commandBuffer, const Layout& finalLayout);
        void cmdCopyFromImage(VkCommandBuffer commandBuffer, const Image& srcImage);

        [[nodiscard]] VkImage getHandle() const { return m_image; }
        [[nodiscard]] VkImageView getImageView() const { return m_imageView; }
        [[nodiscard]] VmaAllocation getAllocation() const { return m_allocation; }
        [[nodiscard]] const CreateInfo& getCreateInfo() const { return m_createInfo; }

    private:
        Image(const std::shared_ptr<Device>& device, VkImage existingImage, const CreateInfo& createInfo, bool ownsImage = false);
        bool m_ownsImage{true}; // To avoid destroying swapchain images

        void createImage();
        void createImageView();
        void cleanup();
        [[nodiscard]] bool supportsLinearBlitting() const;

    private:
        std::shared_ptr<Device> m_device;
        CreateInfo m_createInfo;
        Layout m_currentLayout{
            .layout = m_createInfo.initialLayout,
            .accessMask = 0,
            .stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
        };

        VkImage m_image{VK_NULL_HANDLE};
        VkImageView m_imageView{VK_NULL_HANDLE};
        VmaAllocation m_allocation{VK_NULL_HANDLE};
};
