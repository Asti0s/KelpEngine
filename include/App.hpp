#pragma once

#include "Buffer.hpp"
#include "Camera.hpp"
#include "DescriptorManager.hpp"
#include "Device.hpp"
#include "Image.hpp"
#include "Swapchain.hpp"
#include "Window.hpp"

#include "fastgltf/types.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glslang/Public/ShaderLang.h"
#include "vulkan/vulkan_core.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
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

        void handleEvents(float deltaTime);


    private: // Assets
        struct Vertex {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec2 uv;
        };

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
            Mesh *mesh;
            std::vector<VkAccelerationStructureInstanceKHR> instances;
        };

        std::vector<Mesh> m_meshes;
        std::vector<MeshInstance> m_meshInstances;

        Primitive loadPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive);
        void loadMeshes(fastgltf::Asset& asset);

        void loadGltfNode(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Node& node, const glm::mat4& parentTransform = glm::mat4(1));
        void loadGltfScene(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Scene& scene);

        void loadAssetsFromFile(const char *filePath);



        struct GpuPrimitiveInstance {
            VkDeviceAddress vertexBufferAddress;
            VkDeviceAddress indexBufferAddress;
            int materialIndex;
        };

        std::vector<GpuPrimitiveInstance> m_gpuPrimitiveInstances;
        std::unique_ptr<Buffer> m_gpuPrimitiveInstancesBuffer;


        struct Texture {
            Image *image;
            VkSampler *sampler;
            uint32_t bindlessId;
        };

        std::vector<Image> m_images;
        std::vector<VkSampler> m_samplers;
        std::vector<Texture> m_textures;

        Image loadImage(uint8_t *data, const glm::ivec2& size);
        void loadImages(const std::filesystem::path& filePath, fastgltf::Asset& asset);
        void loadSamplers(const fastgltf::Asset& asset);
        void loadTextures(fastgltf::Asset& asset);



        struct GpuMaterial {
            // Textures
            int baseColorTextureIndex;
            int normalTextureIndex;
            int metallicRoughnessTextureIndex;
            int emissiveTextureIndex;

            // Factors
            glm::vec4 baseColorFactor;
            float metallicFactor;
            float roughnessFactor;
            glm::vec3 emissiveFactor;

            // Params
            int alphaMode;
            float alphaCutoff;
        };

        std::unique_ptr<Buffer> m_gpuMaterials;

        void loadMaterials(const fastgltf::Asset& asset);


    private: // Goofy raytracing
        struct PushConstantData {
            glm::mat4 inverseView;
            glm::mat4 inverseProjection;
            VkDeviceAddress gpuPrimitiveInstancesBufferAddress;
            VkDeviceAddress gpuMaterialsBufferAddress;
        };

        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_raytracingProperties{};
        VkPipelineLayout m_pipelineLayout{};
        VkPipeline m_raytracingPipeline{};

        std::unique_ptr<Image> m_outputImage;
        uint32_t m_outputImageBindlessId{};

        void createRaytracingPipeline();
        void createShaderBindingTable();

        VkShaderModule compileShader(const std::string& path, EShLanguage stage);



    private:
        const std::shared_ptr<Window> m_window = std::make_shared<Window>(glm::ivec2(1280, 720), "Kelp Engine", true);
        const std::shared_ptr<Device> m_device = std::make_shared<Device>(m_window);
        Swapchain m_swapchain{m_device, m_window->getSize()};
        DescriptorManager m_descriptorManager{m_device};

        Camera m_camera{m_window};

        std::unique_ptr<Buffer> m_topLevelAccelerationStructureBuffer;
        VkAccelerationStructureKHR m_topLevelAccelerationStructure{};
        uint32_t m_topLevelAccelerationStructureBindlessId{};

        std::unique_ptr<Buffer> m_raygenShaderBindingTable;
        void *m_mappedRaygenShaderBindingTable{};

        std::unique_ptr<Buffer> m_missShaderBindingTable;
        void *m_mappedMissShaderBindingTable{};

        std::unique_ptr<Buffer> m_hitShaderBindingTable;
        void *m_mappedHitShaderBindingTable{};
};
