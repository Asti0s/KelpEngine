#include "App.hpp"

#include "Vulkan/VkDevice.hpp"
#include "Vulkan/VkSwapchain.hpp"

#include "glm/ext/vector_int2.hpp"
#include <tuple>
#include <vulkan/vulkan_core.h>

App::App() {
    m_window->setResizeCallback([&](const glm::ivec2& size) {
        m_swapchain.resize(size);
    });

    loadAssetsFromFile("../../RibJobEngine/assets/Sponza/Sponza.gltf");
}

App::~App() {
    for (const auto& [name, mesh] : m_meshes) {
        vkDestroyAccelerationStructureKHR(m_device->getHandle(), mesh.accelerationStructure.handle, VK_NULL_HANDLE);
    }
}

void App::run() {
    while (m_window->isOpen()) {
        m_window->pollEvents();

        std::ignore = m_swapchain.beginFrame();
        std::ignore = m_swapchain.prepareNewImage();
        m_swapchain.endFrame();
    }
}
