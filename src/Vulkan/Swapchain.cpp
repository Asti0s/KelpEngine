#include "Vulkan/Swapchain.hpp"

#include "Config.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Utils.hpp"

#include "glm/ext/vector_int2.hpp"
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

Swapchain::Swapchain(const std::shared_ptr<Device>& device, const glm::ivec2& size) : m_device(device) {
    createSwapchain(size);
    createImageViews();
    createRenderCommandBuffers();
    createSyncObjects();
}

Swapchain::~Swapchain() {
    m_device->waitIdle();

    for (uint32_t i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_inFlightFences[i] != nullptr)
            vkDestroyFence(m_device->getHandle(), m_inFlightFences[i], nullptr);

        if (m_renderFinishedSemaphores[i] != nullptr)
            vkDestroySemaphore(m_device->getHandle(), m_renderFinishedSemaphores[i], nullptr);

        if (m_imageAvailableSemaphores[i] != nullptr)
            vkDestroySemaphore(m_device->getHandle(), m_imageAvailableSemaphores[i], nullptr);
    }

    if (!m_renderCommandBuffers.empty())
        vkFreeCommandBuffers(m_device->getHandle(), m_device->getCommandPool(Device::Graphics), static_cast<uint32_t>(m_renderCommandBuffers.size()), m_renderCommandBuffers.data());

    for (VkImageView imageView : m_imageViews)
        if (imageView != nullptr)
            vkDestroyImageView(m_device->getHandle(), imageView, nullptr);

    if (m_swapchain != nullptr)
        vkDestroySwapchainKHR(m_device->getHandle(), m_swapchain, nullptr);
}

void Swapchain::resize(const glm::ivec2& size) {
    // Cleanup
    m_device->waitIdle();
    for (VkImageView imageView : m_imageViews)
        vkDestroyImageView(m_device->getHandle(), imageView, nullptr);
    vkDestroySwapchainKHR(m_device->getHandle(), m_swapchain, nullptr);

    // Recreate
    createSwapchain(size);
    createImageViews();
}

void Swapchain::createRenderCommandBuffers() {
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_device->getCommandPool(Device::Graphics),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = Config::MAX_FRAMES_IN_FLIGHT
    };

    VK_CHECK(vkAllocateCommandBuffers(m_device->getHandle(), &allocInfo, m_renderCommandBuffers.data()));
}

void Swapchain::createImageViews() {
    VK_CHECK(vkGetSwapchainImagesKHR(m_device->getHandle(), m_swapchain, &m_imageCount, nullptr));

    m_images.resize(m_imageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(m_device->getHandle(), m_swapchain, &m_imageCount, m_images.data()));

    m_imageViews.resize(m_imageCount);
    for (uint32_t i = 0; i < m_imageCount; i++) {
        const VkImageViewCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_imageFormat,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        VK_CHECK(vkCreateImageView(m_device->getHandle(), &createInfo, nullptr, &m_imageViews[i]));
    }
}

void Swapchain::createSwapchain(const glm::ivec2& size) {
    VkSurfaceCapabilitiesKHR capabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->getPhysicalDevice(), m_device->getSurface(), &capabilities));

    m_imageCount = Config::MAX_FRAMES_IN_FLIGHT + 1;
    if (capabilities.maxImageCount > 0 && m_imageCount > capabilities.maxImageCount)
        m_imageCount = capabilities.maxImageCount;


    // Extent selection
    m_extent.width = static_cast<uint32_t>(size.x);
    m_extent.height = static_cast<uint32_t>(size.y);
    m_extent.width = std::clamp(m_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    m_extent.height = std::clamp(m_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);


    // Image format selection
    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->getPhysicalDevice(), m_device->getSurface(), &formatCount, nullptr));

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->getPhysicalDevice(), m_device->getSurface(), &formatCount, formats.data()));

    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM) {
            m_imageFormat = format.format;
            break;
        }
    }

    if (m_imageFormat == VK_FORMAT_UNDEFINED)
        m_imageFormat = formats[0].format;


    // Present mode selection
    uint32_t presentModeCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(m_device->getPhysicalDevice(), m_device->getSurface(), &presentModeCount, nullptr));

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(m_device->getPhysicalDevice(), m_device->getSurface(), &presentModeCount, presentModes.data()));

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const VkPresentModeKHR& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = mode;
            break;
        }
    }


    // Swapchain creation
    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_device->getSurface(),
        .minImageCount = m_imageCount,
        .imageFormat = m_imageFormat,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = m_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };

    VK_CHECK(vkCreateSwapchainKHR(m_device->getHandle(), &createInfo, nullptr, &m_swapchain));
}

void Swapchain::createSyncObjects() {
    constexpr VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    constexpr VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (uint8_t i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(m_device->getHandle(), &semaphoreCreateInfo, nullptr, &m_imageAvailableSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(m_device->getHandle(), &semaphoreCreateInfo, nullptr, &m_renderFinishedSemaphores[i]));
        VK_CHECK(vkCreateFence(m_device->getHandle(), &fenceCreateInfo, nullptr, &m_inFlightFences[i]));
    }
}

void Swapchain::beginFrame() {
    // Wait for fence to signal (signaled by endFrame)
    VK_CHECK(vkWaitForFences(m_device->getHandle(), 1, &m_inFlightFences[m_currentFrameIndex], VK_TRUE, std::numeric_limits<uint64_t>::max()));
    VK_CHECK(vkResetFences(m_device->getHandle(), 1, &m_inFlightFences[m_currentFrameIndex]));


    // Begin command buffer
    constexpr VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    VkCommandBuffer commandBuffer = m_renderCommandBuffers[m_currentFrameIndex];
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
}

void Swapchain::acquireImage() {
    VK_CHECK(vkAcquireNextImageKHR(m_device->getHandle(), m_swapchain, std::numeric_limits<uint64_t>::max(), m_imageAvailableSemaphores[m_currentFrameIndex], VK_NULL_HANDLE, &m_currentImageIndex));
}

void Swapchain::endFrame() {
    VkCommandBuffer commandBuffer = m_renderCommandBuffers[m_currentFrameIndex];


    // Submit command buffer to graphics queue
    constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &m_imageAvailableSemaphores[m_currentFrameIndex],
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &m_renderFinishedSemaphores[m_currentFrameIndex]
    };

    VK_CHECK(vkEndCommandBuffer(commandBuffer));
    VK_CHECK(vkQueueSubmit(m_device->getQueue(Device::Graphics), 1, &submitInfo, m_inFlightFences[m_currentFrameIndex]));


    // Present image
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrameIndex],
        .swapchainCount = 1,
        .pSwapchains = &m_swapchain,
        .pImageIndices = &m_currentImageIndex
    };

    VK_CHECK(vkQueuePresentKHR(m_device->getQueue(Device::Graphics), &presentInfo));


    // Advance frame index
    m_currentFrameIndex = (m_currentFrameIndex + 1) % Config::MAX_FRAMES_IN_FLIGHT;
}
