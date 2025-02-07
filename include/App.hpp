#pragma once

#include "VkDevice.hpp"
#include "Window.hpp"

#include <memory>

class App {
    public:
        App();
        ~App();

        App(const App&) = delete;
        App& operator=(const App&) = delete;

        App(App&&) noexcept = delete;
        App& operator=(App&&) = delete;

        void run();

    private:
        const std::shared_ptr<Window> m_window = std::make_shared<Window>(glm::ivec2(1280, 720), "Kelp Engine", true);
        const std::shared_ptr<Device> m_device = std::make_shared<Device>(m_window);
};
