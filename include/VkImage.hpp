#pragma once

#include "VkDevice.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>

class Image {
    public:
        Image(const std::shared_ptr<Device>& device, const VkExtent3D& extent, VkImageUsageFlags usage, VkFormat format, VkImageType type = VK_IMAGE_TYPE_2D, uint8_t mipLevels = 1, VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);
        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        Image(Image&& other) noexcept;
        Image& operator=(Image&& other) noexcept;


        /**
        * @brief Same as the other copyFromBuffer function, but this one takes a
        * command buffer to record the copy commands into
        *
        * @param commandBuffer the command buffer to record the copy commands into
        * @param buffer the buffer to copy the data from
        */
        void cmdCopyFromBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer);

        /**
         * @brief Generate mipmaps for the image
         * The image must be in the following state:
         * - Stage:  VK_PIPELINE_STAGE_TRANSFER_BIT
         * - Access: VK_ACCESS_MEMORY_WRITE_BIT
         * - Layout: VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
         *
         * After the generation, the image will be in the following state:
         * - Stage:  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
         * - Access: VK_ACCESS_SHADER_READ_BIT
         * - Layout: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
         *
         * @param commandBuffer the command buffer to record the mipmaps generation commands into
         */
        void cmdGenerateMipmaps(VkCommandBuffer commandBuffer);

        /**
         * @brief Transition the image to a new layout
         *
         * @param commandBuffer the command buffer to record the barrier command into
         * @param srcStageMask the source pipeline stage
         * @param dstStageMask the destination pipeline stage
         * @param srcAccessMask the source access mask
         * @param dstAccessMask the destination access mask
         * @param oldLayout the old layout
         * @param newLayout the new layout
         */
        void cmdImagebarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout);


        /* Getters */
        [[nodiscard]] VkImage getHandle() const { return m_image; }
        [[nodiscard]] VkImageView getImageView() const { return m_imageView; }
        [[nodiscard]] VmaAllocation getAllocation() const { return m_allocation; }

        [[nodiscard]] const VkExtent3D& getExtent() const { return m_extent; }
        [[nodiscard]] VkFormat getFormat() const { return m_format; }
        [[nodiscard]] uint8_t getMipLevels() const { return m_mipLevels; }
        [[nodiscard]] VkImageAspectFlags getAspectFlags() const { return m_aspectFlags; }


    private:
        void cleanup();


    private:
        std::shared_ptr<Device> m_device;

        VkImage m_image{};
        VkImageView m_imageView{};
        VmaAllocation m_allocation{};

        VkExtent3D m_extent{};
        VkFormat m_format{};
        uint8_t m_mipLevels{};
        VkImageAspectFlags m_aspectFlags{};
};
