#pragma once

#include "Viewer/Config.hpp"
#include "Device.hpp"
#include "Image.hpp"

#include "glm/ext/vector_int2.hpp"
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class Swapchain {
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

        /**
         * @brief Begin the frame by acquiring an image and beginning a new command buffer
         *
         * @return The command buffer to record commands into
         */
        VkCommandBuffer beginFrame();

        /**
         * @brief End the frame by submitting the command buffer and presenting the image
         *
         * @param commandBuffer The command buffer to submit
         * @param waitStage The pipeline stage to wait on before presenting the image
         */
        void endFrame(VkCommandBuffer commandBuffer, VkPipelineStageFlags waitStage);


        /* Getters */
        [[nodiscard]] VkSwapchainKHR                    getHandle()                 const noexcept { return m_swapchain; }
        [[nodiscard]] VkFormat                          getImageFormat()            const noexcept { return m_imageFormat; }
        [[nodiscard]] const VkExtent2D&                 getExtent()                 const noexcept { return m_extent; }
        [[nodiscard]] uint32_t                          getImageCount()             const noexcept { return m_imageCount; }

        [[nodiscard]] uint32_t                          getCurrentFrameIndex()      const noexcept { return m_currentFrameIndex; }
        [[nodiscard]] uint32_t                          getCurrentImageIndex()      const noexcept { return m_currentImageIndex; }
        [[nodiscard]] const std::unique_ptr<Image>&     getCurrentImage()           const noexcept { return m_images[m_currentImageIndex]; }


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
        std::vector<std::unique_ptr<Image>> m_images;
        std::array<VkCommandBuffer, Config::MAX_FRAMES_IN_FLIGHT> m_renderCommandBuffers{};

        // Sync objects
        std::array<VkSemaphore, Config::MAX_FRAMES_IN_FLIGHT> m_imageAvailableSemaphores{};
        std::array<VkSemaphore, Config::MAX_FRAMES_IN_FLIGHT> m_renderFinishedSemaphores{};
        std::array<VkFence, Config::MAX_FRAMES_IN_FLIGHT> m_inFlightFences{};

        // Frame infos
        uint32_t m_currentFrameIndex = 0;
        uint32_t m_currentImageIndex = 0;
};
