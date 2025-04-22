#pragma once

#include "Camera.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/DescriptorManager.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Swapchain.hpp"
#include "Window.hpp"
#include "shared.hpp"

#include "fastgltf/types.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_int2.hpp"
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

class App {
    public:
        App();
        ~App();

        App(const App&) = delete;
        App& operator=(const App&) = delete;

        App(App&&) noexcept = delete;
        App& operator=(App&&) = delete;

        void run();


    private: // Assets
        struct AccelerationStructure {
            VkAccelerationStructureKHR handle;
            VkDeviceAddress deviceAddress;
            Buffer buffer;
        };

        struct Primitive {
            Buffer vertexBuffer;
            Buffer indexBuffer;
            uint32_t indexCount;
            AccelerationStructure accelerationStructure;
            int materialIndex;
        };

        struct Mesh {
            std::vector<Primitive> primitives;
        };

        struct MeshInstance {
            std::shared_ptr<Mesh> mesh;
            std::vector<VkAccelerationStructureInstanceKHR> instances;
        };

        struct Texture {
            std::shared_ptr<Image> image;
            VkSampler sampler;
            uint32_t bindlessId;
        };

        std::vector<std::shared_ptr<Mesh>> m_meshes;
        std::vector<MeshInstance> m_meshInstances;
        std::vector<PrimitiveInstance> m_primitiveInstances;
        std::unique_ptr<Buffer> m_primitiveInstancesBuffer;

        std::map<uint32_t, std::shared_ptr<Image>> m_images;
        std::unique_ptr<Buffer> m_materialBuffer;
        std::vector<VkSampler> m_samplers;
        std::vector<Texture> m_textures;

        void loadMeshes(fastgltf::Asset& asset);
        Primitive loadPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive);
        void loadGltfNode(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Node& node, const glm::mat4& parentTransform = glm::mat4(1));
        void loadGltfScene(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Scene& scene);

        Image loadImage(uint8_t *data, const glm::ivec2& size, std::mutex& commandMutex);
        void loadImages(const std::filesystem::path& filePath, fastgltf::Asset& asset);
        void loadSamplers(const fastgltf::Asset& asset);
        void loadTextures(fastgltf::Asset& asset);
        void loadMaterials(const fastgltf::Asset& asset);

        void loadAssetsFromFile(const char *filePath);


    private: // Raytracing preparation
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_raytracingProperties{};
        VkPipelineLayout m_pipelineLayout{};
        VkPipeline m_raytracingPipeline{};

        std::unique_ptr<Image> m_outputImage;

        void createRaytracingPipeline();
        void createShaderBindingTable();
        void prepareOutputImage();
        void getRaytracingProperties();


    private: // Runtime
        void handleEvents(float deltaTime);
        void traceRays(VkCommandBuffer commandBuffer);
        void transferOutputImageToSwapchain(VkCommandBuffer commandBuffer);
        void bindDescriptors(VkCommandBuffer commandBuffer);
        void updateWindowTitle(float deltaTime);


    private:
        const std::shared_ptr<Window> m_window = std::make_shared<Window>(glm::ivec2(1280, 720), "Kelp Engine", true);
        const std::shared_ptr<Device> m_device = std::make_shared<Device>(m_window);
        Swapchain m_swapchain{m_device, m_window->getSize()};
        DescriptorManager m_descriptorManager{m_device};

        Camera m_camera{m_window};

        std::unique_ptr<Buffer> m_topLevelAccelerationStructureBuffer;
        VkAccelerationStructureKHR m_topLevelAccelerationStructure{};

        std::unique_ptr<Buffer> m_raygenShaderBindingTable;
        void *m_mappedRaygenShaderBindingTable{};

        std::unique_ptr<Buffer> m_missShaderBindingTable;
        void *m_mappedMissShaderBindingTable{};

        std::unique_ptr<Buffer> m_hitShaderBindingTable;
        void *m_mappedHitShaderBindingTable{};
};
