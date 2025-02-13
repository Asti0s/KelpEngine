#pragma once

#include "Config.hpp"
#include "VkDevice.hpp"

#include "glm/ext/vector_int2.hpp"
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Vk {

    class Swapchain {
        public:
            struct FrameInfo {
                uint32_t frameIndex;
                VkCommandBuffer commandBuffer;
            };


        public:
            Swapchain(const std::shared_ptr<Device>& device, const glm::ivec2& size);
            ~Swapchain();

            Swapchain(const Swapchain&) = delete;
            Swapchain& operator=(const Swapchain&) = delete;

            Swapchain(Swapchain&& other) = delete;
            Swapchain& operator=(Swapchain&& other) = delete;


            /**
            * @brief Recreate the swapchain with a new size
            *
            * @param size The new size of the swapchain
            */
            void resize(const glm::ivec2& size);

            void beginFrame();
            void acquireImage();
            void endFrame();


            /* Getters */
            [[nodiscard]] VkSwapchainKHR    getHandle()                 const noexcept { return m_swapchain; }
            [[nodiscard]] VkFormat          getImageFormat()            const noexcept { return m_imageFormat; }
            [[nodiscard]] const VkExtent2D& getExtent()                 const noexcept { return m_extent; }
            [[nodiscard]] uint32_t          getImageCount()             const noexcept { return m_imageCount; }
            [[nodiscard]] uint32_t          getCurrentFrameIndex()      const noexcept { return m_currentFrameIndex; }
            [[nodiscard]] uint32_t          getCurrentImageIndex()      const noexcept { return m_currentImageIndex; }
            [[nodiscard]] VkCommandBuffer   getCurrentCommandBuffer()   const noexcept { return m_renderCommandBuffers[m_currentFrameIndex]; }
            [[nodiscard]] VkImageView       getCurrentImageView()       const noexcept { return m_imageViews[m_currentImageIndex]; }
            [[nodiscard]] VkImage           getCurrentImage()           const noexcept { return m_images[m_currentImageIndex]; }


        private:
            void createSwapchain(const glm::ivec2& size);
            void createImageViews();
            void createRenderCommandBuffers();
            void createSyncObjects();


        private:
            std::shared_ptr<Device> m_device;

            // Swapchain related
            VkSwapchainKHR m_swapchain{};
            VkFormat m_imageFormat{};
            VkExtent2D m_extent{};
            uint32_t m_imageCount = 0;

            // Per frame data
            std::vector<VkImageView> m_imageViews;
            std::vector<VkImage> m_images;
            std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_renderCommandBuffers{};

            // Sync objects
            std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_imageAvailableSemaphores{};
            std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_renderFinishedSemaphores{};
            std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_inFlightFences{};

            // Frame infos
            uint32_t m_currentFrameIndex = 0;
            uint32_t m_currentImageIndex = 0;
    };

}   // namespace Vk
