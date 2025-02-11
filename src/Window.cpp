#include "Window.hpp"

#include "GLFW/glfw3.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_int2.hpp"

#include <cassert>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
    std::function<void(const glm::ivec2& newSize)> resizeCallback = [](const glm::ivec2& newSize) {};        // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    std::function<void(const glm::ivec2& position)> cursorPosCallback = [](const glm::ivec2& position) {};   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    std::function<void(const glm::vec2& offset)> scrollCallback = [](const glm::vec2& offset) {};            // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}   // namespace

Window::Window(const glm::ivec2& dimensions, const char* title, bool resizable) {
    assert(!m_window);
    if (!static_cast<bool>(glfwInit()) || !static_cast<bool>(glfwVulkanSupported()))
        throw std::runtime_error("Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, static_cast<int>(resizable));

    m_window = glfwCreateWindow(dimensions.x, dimensions.y, title, nullptr, nullptr);
    if (m_window == nullptr)
        throw std::runtime_error("Failed to create window");

    glfwSetWindowUserPointer(m_window, this);
}

Window::~Window() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Window::close() {
    assert(m_window);
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

bool Window::isOpen() const {
    assert(m_window);
    return !static_cast<bool>(glfwWindowShouldClose(m_window));
}

glm::ivec2 Window::getSize() const {
    assert(m_window);
    glm::ivec2 size{};
    glfwGetWindowSize(m_window, reinterpret_cast<int*>(&size.x), reinterpret_cast<int*>(&size.y));
    return size;
}

void Window::setTitle(const char* title) {
    assert(m_window);
    glfwSetWindowTitle(m_window, title);
}

void Window::setCursorVisible(bool visible) {
    assert(m_window);
    glfwSetInputMode(m_window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void Window::setCursorPosition(const glm::ivec2& position) {
    assert(m_window);
    glfwSetCursorPos(m_window, position.x, position.y);
}

glm::ivec2 Window::getCursorPosition() const {
    assert(m_window);
    double xpos = 0;
    double ypos = 0;
    glfwGetCursorPos(m_window, &xpos, &ypos);
    return {static_cast<int>(xpos), static_cast<int>(ypos)};
}

void Window::pollEvents() {
    assert(m_window);
    glfwPollEvents();
}

bool Window::isKeyPressed(int key) const {
    assert(m_window);
    return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool Window::isMouseButtonPressed(int mouseButton) const {
    assert(m_window);
    return glfwGetMouseButton(m_window, mouseButton) == GLFW_PRESS;
}

std::vector<const char *> Window::getRequiredVulkanExtensions() {
    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    return {extensions, extensions + extensionCount};
}

void Window::setResizeCallback(std::function<void(const glm::ivec2& newSize)> callback) {
    assert(m_window);
    resizeCallback = std::move(callback);
    glfwSetWindowSizeCallback(m_window, [](GLFWwindow* /* UNUSED */, int width, int height) {
        resizeCallback({width, height});
    });
}

void Window::setCursorPosCallback(std::function<void(const glm::ivec2& position)> callback) {
    assert(m_window);
    cursorPosCallback = std::move(callback);
    glfwSetCursorPosCallback(m_window, [](GLFWwindow* /* UNUSED */, double xpos, double ypos) {
        cursorPosCallback({static_cast<int>(xpos), static_cast<int>(ypos)});
    });
}

void Window::setScrollCallback(std::function<void(const glm::vec2& offset)> callback) {
    assert(m_window);
    scrollCallback = std::move(callback);
    glfwSetScrollCallback(m_window, [](GLFWwindow* /* UNUSED */, double xoffset, double yoffset) {
        scrollCallback({xoffset, yoffset});
    });
}
