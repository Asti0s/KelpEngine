#pragma once

#include "Window.hpp"

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

#include <memory>

class Camera {
    public:
        Camera(const std::shared_ptr<Window>& window);
        ~Camera() = default;

        Camera(const Camera&) = default;
        Camera& operator=(const Camera&) = default;

        Camera(Camera&&) = default;
        Camera& operator=(Camera&&) = default;


        /**
         * @brief Update the camera's position and view matrix
         * depending on the user's input.
         *
         * @param deltaTime The time between the last frame and the current frame.
         */
        void update(float deltaTime) noexcept;

        /**
         * @brief Set the Perspective matrix of the camera.
         *
         * @param fov The field of view.
         * @param aspect The aspect ratio.
         * @param near The near plane.
         * @param far The far plane.
         */
        void setPerspective(float fov, float aspect, float near, float far) noexcept;

        /**
         * @brief Reset the mouse position to the center of the
         * window to avoid sudden camera movements.
         * This function should be used after showing the cursor if it was hidden.
         */
        void resetMousePosition() noexcept;

        /**
         * @brief Disable the cursor callback to avoid camera movement
         * based on mouse input.
         *
         * @param lock If true, the cursor will be locked in the center of the window.
         */
        void disableCursorCallback(bool lock) noexcept;


        /* Setters */
        void setPosition(const glm::vec3& position) noexcept { m_position = position; }
        void setFront(const glm::vec3& front) noexcept { m_front = front; }
        void setUpVector(const glm::vec3& upVector) noexcept { m_upVector = upVector; }


        /* Getters */
        [[nodiscard]] const glm::mat4& getViewMatrix() const noexcept { return m_viewMatrix; }
        [[nodiscard]] const glm::mat4& getProjectionMatrix() const noexcept { return m_projectionMatrix; };

        [[nodiscard]] const glm::vec3& getPosition() const noexcept { return m_position; }
        [[nodiscard]] const glm::vec3& getFront() const noexcept { return m_front; }
        [[nodiscard]] const glm::vec3& getUpVector() const noexcept { return m_upVector; }


    private:
        std::shared_ptr<Window> m_window;

        glm::mat4 m_viewMatrix{1};
        glm::mat4 m_projectionMatrix{1};

        glm::vec3 m_position{0, 0, 0};
        glm::vec3 m_front{1, 0, 0};
        glm::vec3 m_upVector{0, 1, 0};

        bool m_isFirstMouse = true;
        bool m_isCursorLocked = false;
        float m_lastX = 0;
        float m_lastY = 0;
        float m_yaw = 0;
        float m_pitch = 0;
};
