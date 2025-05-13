#pragma once

#include "Viewer/Camera.hpp"
#include "Viewer/Window.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/DescriptorManager.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Swapchain.hpp"
#include "omm.hpp"
#include "shared.hpp"

#include "glm/ext/vector_int2.hpp"
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

class Viewer {
    public:
        Viewer();
        ~Viewer();

        Viewer(const Viewer&) = delete;
        Viewer& operator=(const Viewer&) = delete;

        Viewer(Viewer&&) noexcept = delete;
        Viewer& operator=(Viewer&&) = delete;

        void run(const std::filesystem::path& filePath);


    private: // Assets
        struct AccelerationStructure {
            VkAccelerationStructureKHR handle;
            VkDeviceAddress deviceAddress;
            Buffer buffer;
            std::unique_ptr<Buffer> micromapBuffer;
            VkMicromapEXT micromap;
        };

        struct Mesh {
            Buffer vertexBuffer;
            Buffer indexBuffer;
            uint32_t indexCount;
            AccelerationStructure accelerationStructure;
            int materialIndex;
        };

        struct Texture {
            std::shared_ptr<Image> image;
            uint32_t bindlessId;
        };

        std::vector<Texture> m_albedoTextures;
        std::vector<Texture> m_alphaTextures;
        std::vector<Texture> m_normalTextures;
        std::vector<Texture> m_metallicRoughnessTextures;
        std::vector<Texture> m_emissiveTextures;

        omm::Cpu::DeserializedResult m_ommDeserializedResult = nullptr;
        std::vector<omm::Cpu::BakeResultDesc> m_ommBakeResults;

        std::vector<std::shared_ptr<Mesh>> m_meshes;
        std::vector<VkAccelerationStructureInstanceKHR> m_accelerationStructureInstances;
        std::vector<Material> m_materials;

        std::unique_ptr<Buffer> m_materialBuffer;
        std::unique_ptr<Buffer> m_meshInstanceBuffer;
        VkSampler m_defaultSampler{};

        void loadAssetsFromFile(const std::filesystem::path& filePath);
        void loadAndUploadTextureCollection(const std::filesystem::path& filePath, std::ifstream& file, std::vector<Texture>& targetCollection, VkFormat textureFormat, int channelCount);
        void loadMaterials(std::ifstream& file);
        void loadOMMs(std::ifstream& file);
        void loadMeshes(std::ifstream& file);
        void loadMeshInstances(std::ifstream& file);
        static void funcTime(const std::string& context, const std::function<void()>& func);


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
