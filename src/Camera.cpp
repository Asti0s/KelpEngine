/*
** EPITECH PROJECT, 2024
** zappy
** File description:
** Camera
*/

#include "Camera.hpp"

#include "GLFW/glfw3.h"
#include "Window.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

Camera::Camera(const std::shared_ptr<Window>& window) : m_window(window) {
}

void Camera::disableCursorCallback(bool lock) noexcept {
    m_isCursorLocked = lock;
}

void Camera::resetMousePosition() noexcept {
    m_isFirstMouse = true;
}

void Camera::setPerspective(float fov, float aspect, float near, float far) noexcept {
    m_projectionMatrix = {};
    m_projectionMatrix = glm::perspective(glm::radians(fov), aspect, near, far);
}

void Camera::update(float deltaTime) noexcept {
    const float offset = deltaTime * m_cameraSpeed;


    // Mouse movement handling
    if (!m_isCursorLocked) {
        const glm::ivec2 cursorPos = m_window->getCursorPosition();

        if (m_isFirstMouse) {
            m_lastX = static_cast<float>(cursorPos.x);
            m_lastY = static_cast<float>(cursorPos.y);
            m_isFirstMouse = false;
        }

        float xOffset = static_cast<float>(cursorPos.x) - m_lastX;
        float yOffset = m_lastY - static_cast<float>(cursorPos.y);
        m_lastX = static_cast<float>(cursorPos.x);
        m_lastY = static_cast<float>(cursorPos.y);

        const float sensitivity = 0.1;
        xOffset *= sensitivity;
        yOffset *= sensitivity;

        m_yaw += xOffset;

        m_pitch -= yOffset;
        m_pitch = std::max(-89.F, std::min(89.F, m_pitch));

        const glm::vec3 direction = {
            cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch)),
            sin(glm::radians(-m_pitch)),
            sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch))
        };

        m_front = glm::normalize(direction);
    }


    // Keyboard movement handling
    if (m_window->isKeyPressed(GLFW_KEY_W))
        m_position += offset * m_front;
    if (m_window->isKeyPressed(GLFW_KEY_S))
        m_position -= offset * m_front;
    if (m_window->isKeyPressed(GLFW_KEY_A))
        m_position -= glm::normalize(glm::cross(m_front, m_upVector)) * offset;
    if (m_window->isKeyPressed(GLFW_KEY_D))
        m_position += glm::normalize(glm::cross(m_front, m_upVector)) * offset;
    if (m_window->isKeyPressed(GLFW_KEY_SPACE))
        m_position += offset * m_upVector;
    if (m_window->isKeyPressed(GLFW_KEY_C))
        m_position -= offset * m_upVector;


    // View matrix update
    m_viewMatrix = glm::lookAt(m_position, m_position + m_front, m_upVector);
}
