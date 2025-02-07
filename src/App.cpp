#include "App.hpp"

#include "VkSwapchain.hpp"

#include "glm/ext/vector_int2.hpp"
#include <vulkan/vulkan_core.h>

App::App() {
    m_window->setResizeCallback([&](const glm::ivec2& size) {
        m_swapchain.resize(size);
    });
}

App::~App() {
}

void App::run() {
    while (m_window->isOpen()) {
        m_window->pollEvents();

        const Swapchain::FrameInfo frameInfo = m_swapchain.beginFrame();
        VkImageView swapchainImage = m_swapchain.prepareNewImage();
        m_swapchain.endFrame();
    }
}
