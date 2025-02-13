#pragma once

#include "Camera.hpp"
#include "Vulkan/VkBindlessManager.hpp"
#include "Vulkan/VkBuffer.hpp"
#include "Vulkan/VkDevice.hpp"
#include "Vulkan/VkImage.hpp"
#include "Vulkan/VkSwapchain.hpp"
#include "Window.hpp"

#include "fastgltf/types.hpp"
#include "flecs.h"  // NOLINT
#include "flecs/addons/cpp/world.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glslang/Public/ShaderLang.h"
#include "vulkan/vulkan_core.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

class App {
    public:
        App();
        ~App();

        App(const App&) = delete;
        App& operator=(const App&) = delete;

        App(App&&) noexcept = delete;
        App& operator=(App&&) = delete;

        void run();

        void handleEvents(float deltaTime);


    private: // Assets
        struct Vertex {
            glm::vec3 position;
            glm::vec3 normal;
        };

        struct AccelerationStructure {
            VkAccelerationStructureKHR handle;
            VkDeviceAddress deviceAddress;
            Vk::Buffer buffer;
        };

        struct Mesh {
            Vk::Buffer vertexBuffer;
            VkDeviceAddress vertexBufferDeviceAddress;
            Vk::Buffer indexBuffer;
            VkDeviceAddress indexBufferDeviceAddress;
            uint32_t indexCount;
            AccelerationStructure accelerationStructure;
        };

        struct MeshComponent {
            Vk::Buffer *vertexBuffer;
            Vk::Buffer *indexBuffer;
            uint32_t indexCount;
            glm::mat4 transform;
            VkAccelerationStructureInstanceKHR accelerationStructureInstance;
        };

        std::map<std::string, Mesh> m_meshes;

        void loadPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, const std::string& name);
        void loadGltfNode(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Node& node, const glm::mat4& parentTransform = glm::mat4(1));
        void loadGltfScene(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Scene& scene);

        void loadAssetsFromFile(const char *filePath);


    private: // Goofy raytracing
        struct PushConstantData {
            glm::mat4 inverseView;
            glm::mat4 inverseProjection;
        };

        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_raytracingProperties{};
        VkPipelineLayout m_pipelineLayout{};
        VkPipeline m_raytracingPipeline{};

        std::unique_ptr<Vk::Image> m_outputImage;
        uint32_t m_outputImageBindlessId{};

        void createRaytracingPipeline();
        void createShaderBindingTable();

        VkShaderModule compileShader(const std::string& path, EShLanguage stage);



    private:
        const std::shared_ptr<Window> m_window = std::make_shared<Window>(glm::ivec2(1280, 720), "Kelp Engine", true);
        const std::shared_ptr<Vk::Device> m_device = std::make_shared<Vk::Device>(m_window);
        Vk::Swapchain m_swapchain{m_device, m_window->getSize()};
        Vk::BindlessManager m_bindlessManager{m_device};

        flecs::world m_ecs;

        Camera m_camera{m_window};

        std::unique_ptr<Vk::Buffer> m_topLevelAccelerationStructureBuffer;
        VkAccelerationStructureKHR m_topLevelAccelerationStructure{};
        uint32_t m_topLevelAccelerationStructureBindlessId{};

        std::unique_ptr<Vk::Buffer> m_raygenShaderBindingTable;
        void *m_mappedRaygenShaderBindingTable{};

        std::unique_ptr<Vk::Buffer> m_missShaderBindingTable;
        void *m_mappedMissShaderBindingTable{};

        std::unique_ptr<Vk::Buffer> m_hitShaderBindingTable;
        void *m_mappedHitShaderBindingTable{};
};
