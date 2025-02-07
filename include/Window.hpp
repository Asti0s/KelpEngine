#pragma once

#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES
#include "GLFW/glfw3.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_int2.hpp"

#include <functional>

class Window {
    public:
        Window(const glm::ivec2& dimensions, const char* title, bool resizable);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        Window(Window&&) noexcept = delete;
        Window& operator=(Window&&) = delete;

        [[nodiscard]] GLFWwindow *getHandle() const { return m_window; }


    public:
        /**
         * @brief Get the vulkan extensions required by the window.
         *
         * @return std::vector<const char *> The required vulkan extensions.
         */
        [[nodiscard]] static std::vector<const char *> getRequiredVulkanExtensions();

        /**
         * @brief Close the window.
         * The window will be closed and the window object will be in an invalid state.
         */
        void close();

        /**
         * @brief Check if the window is open.
         *
         * @return true If the window is open.
         * @return false If the window is closed.
         */
        [[nodiscard]] bool isOpen() const;

        /**
         * @brief Get the size of the window.
         *
         * @return The size of the window (width, height).
         */
        [[nodiscard]] glm::ivec2 getSize() const;

        /**
         * @brief Set the title of the window.
         *
         * @param title The new title of the window.
         */
        void setTitle(const char* title);

        /**
         * @brief Set the cursor visibility.
         *
         * @param visible Whether the cursor should be visible or not.
         */
        void setCursorVisible(bool visible);

        /**
         * @brief Set the cursor position.
         *
         * @param position The new position of the cursor.
         */
        void setCursorPosition(const glm::ivec2& position);

        /**
         * @brief Get the cursor position.
         *
         * @return The position of the cursor (x, y).
         */
        [[nodiscard]] glm::ivec2 getCursorPosition() const;

        /**
         * @brief Poll for events and update the window.
         */
        void pollEvents();

        /**
         * @brief Check if a key is pressed.
         *
         * @param key The glfw key to check.
         * @return true If the key is pressed.
         * @return false If the key is not pressed.
         */
        [[nodiscard]] bool isKeyPressed(int key) const;

        /**
         * @brief Check if a mouse button is pressed.
         *
         * @param button The glfw mouse button to check.
         * @return true If the mouse button is pressed.
         * @return false If the mouse button is not pressed.
         */
        [[nodiscard]] bool isMouseButtonPressed(int mouseButton) const;

        /**
         * @brief Set the resize callback function for the window.
         * The callback will be called when the window is resized.
         *
         * @param callback The callback function to set.
         */
        void setResizeCallback(std::function<void(const glm::ivec2& newSize)> callback);

        /**
         * @brief Set the key callback function for handling mouse movement.
         * The callback will be called when the mouse is moved.
         *
         * @param callback The callback function to set.
         */
        void setCursorPosCallback(std::function<void(const glm::ivec2& position)> callback);

        /**
         * @brief Set the scroll callback function for handling mouse scroll.
         * The callback will be called when the mouse is scrolled.
         *
         * @param callback The callback function to set.
         */
        void setScrollCallback(std::function<void(const glm::vec2& offset)> callback);


    private:
        GLFWwindow* m_window = nullptr;
};
