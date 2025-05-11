#include "Viewer/Viewer.hpp"

#include "Viewer/Vulkan/Buffer.hpp"
#include "Viewer/Vulkan/Device.hpp"
#include "Viewer/Vulkan/Image.hpp"
#include "Viewer/Vulkan/Utils.hpp"
#include "shared.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "fastgltf/types.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/gtx/string_cast.hpp"
#include "stb_image.h"
#include "vk_mem_alloc.h"
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

struct TextureMetaData {
    glm::ivec2 size;
    size_t offset;
};

struct KelpMeshInstance {
    glm::mat4 transform;
    int meshIndex;
};

void Viewer::loadAndUploadTextureCollection(const std::filesystem::path& filePath, std::ifstream& file, std::vector<Texture>& targetCollection, VkFormat textureFormat, int channelCount) {
    // Read texture count
    size_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    targetCollection.resize(count);


    // Read texture metadata (size and offset in the file) to allow for concurrent reading instead
    std::vector<TextureMetaData> textureMetadata(count);

    for (size_t i = 0; i < count; ++i) {
        TextureMetaData& tex = textureMetadata[i];
        file.read(reinterpret_cast<char*>(&tex.size), sizeof(tex.size));
        if (tex.size.x <= 0 || tex.size.y <= 0)
            throw std::runtime_error("Error: Texture size is invalid: " + glm::to_string(tex.size));

        tex.offset = static_cast<size_t>(file.tellg());
        file.seekg(static_cast<std::streamsize>(static_cast<size_t>(tex.size.x) * tex.size.y * channelCount), std::ios::cur);
    }


    // Threads & mutex to avoid using the same command buffer at the same time
    std::vector<std::thread> threads(count);
    std::mutex commandMutex;


    // Iterate through all of the textures of the file and load them
    for (int i = 0; i < textureMetadata.size(); i++) {
        const TextureMetaData& tex = textureMetadata[i];

        threads.emplace_back([&, i]() {
            // Open a new stream to allow for concurrent reading
            std::ifstream file(filePath, std::ios::binary);
            file.seekg(static_cast<std::streamsize>(tex.offset), std::ios::beg);


            // Read texture data
            std::vector<uint8_t> textureData(static_cast<size_t>(tex.size.x) * tex.size.y * channelCount);
            file.read(reinterpret_cast<char*>(textureData.data()), static_cast<std::streamsize>(textureData.size()));
            if (file.gcount() != static_cast<std::streamsize>(textureData.size()))
                throw std::runtime_error("Error: Texture data read failed");


            // Image creation
            uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(tex.size.x, tex.size.y)))) + 1;
            const Image::CreateInfo imageCreateInfo{
                .extent = VkExtent3D{static_cast<uint32_t>(tex.size.x), static_cast<uint32_t>(tex.size.y), 1},
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .format = textureFormat,
                .type = VK_IMAGE_TYPE_2D,
                .mipLevels = static_cast<uint8_t>(mipLevels),
            };
            const std::shared_ptr<Image> image = std::make_shared<Image>(m_device, imageCreateInfo);


            // Staging buffer creation & mapping
            Buffer stagingBuffer = Buffer(m_device, static_cast<size_t>(tex.size.x * tex.size.y * channelCount), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            void *mappedData = nullptr;
            stagingBuffer.map(&mappedData);
            memcpy(mappedData, textureData.data(), static_cast<size_t>(tex.size.x) * tex.size.y * channelCount);
            stagingBuffer.unmap();


            // Data upload to gpu & mipmap generation
            commandMutex.lock();
            VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
                image->cmdTransitionLayout(commandBuffer, Image::Layout{
                    .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .accessMask = 0,
                    .stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                }, Image::Layout{
                    .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .accessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
                });

                image->cmdCopyFromBuffer(commandBuffer, stagingBuffer.getHandle());

                image->cmdGenerateMipmaps(commandBuffer, Image::Layout{
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .accessMask = VK_ACCESS_SHADER_READ_BIT,
                    .stageFlags = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                });
            } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);
            commandMutex.unlock();


            // Adding image to the collection
            targetCollection[i] = Texture{
                .image = image,
                .bindlessId = m_descriptorManager.storeSampledImage(image->getImageView(), m_defaultSampler),
            };
        });
    }


    // Wait for all threads to finish
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void Viewer::loadMaterials(std::ifstream& file) {
    // Read material count
    size_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    m_materials.resize(count);


    // Read material data
    file.read(reinterpret_cast<char*>(m_materials.data()), static_cast<std::streamsize>(sizeof(Material) * count));


    // Transition from material index to bindless index
    for (auto& material : m_materials) {
        if (material.baseColorTexture != -1)
            material.baseColorTexture = static_cast<int>(m_albedoTextures[material.baseColorTexture].bindlessId);
        if (material.alphaTexture != -1)
            material.alphaTexture = static_cast<int>(m_alphaTextures[material.alphaTexture].bindlessId);
        if (material.metallicRoughnessTexture != -1)
            material.metallicRoughnessTexture = static_cast<int>(m_metallicRoughnessTextures[material.metallicRoughnessTexture].bindlessId);
        if (material.normalTexture != -1)
            material.normalTexture = static_cast<int>(m_normalTextures[material.normalTexture].bindlessId);
        if (material.emissiveTexture != -1)
            material.emissiveTexture = static_cast<int>(m_emissiveTextures[material.emissiveTexture].bindlessId);
    }


    // Create material buffer
    const Buffer materialStagingBuffer = Buffer(m_device, m_materials.size() * sizeof(Material), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    void *mappedData = nullptr;
    materialStagingBuffer.map(&mappedData);
    memcpy(mappedData, m_materials.data(), m_materials.size() * sizeof(Material));
    materialStagingBuffer.unmap();


    // Buffer creation
    m_materialBuffer = std::make_unique<Buffer>(m_device, m_materials.size() * sizeof(Material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::Graphics); {
        m_materialBuffer->copyFrom(commandBuffer, materialStagingBuffer.getHandle(), m_materials.size() * sizeof(Material));
    } m_device->endSingleTimeCommands(Device::Graphics, commandBuffer);
}

void Viewer::loadMeshes(std::ifstream& file) {
    // Read mesh count
    size_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    m_meshes.resize(count);


    // Read mesh data
    for (size_t i = 0; i < count; ++i) {
        // Read material index
        size_t materialIndex = 0;
        file.read(reinterpret_cast<char*>(&materialIndex), sizeof(size_t));
        if (materialIndex >= m_materials.size())
            throw std::runtime_error("Error: Material index out of bounds: " + std::to_string(materialIndex) + " >= " + std::to_string(m_materials.size()));


        // Read vertices
        size_t vertexCount = 0;
        file.read(reinterpret_cast<char*>(&vertexCount), sizeof(size_t));
        std::vector<Vertex> vertices(vertexCount);
        file.read(reinterpret_cast<char*>(vertices.data()), static_cast<std::streamsize>(sizeof(Vertex) * vertexCount));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(Vertex) * vertexCount))
            throw std::runtime_error("Error: Vertex data read failed");


        // Read indices
        size_t indexCount = 0;
        file.read(reinterpret_cast<char*>(&indexCount), sizeof(size_t));
        std::vector<uint32_t> indices(indexCount);
        file.read(reinterpret_cast<char*>(indices.data()), static_cast<std::streamsize>(sizeof(uint32_t) * indexCount));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(uint32_t) * indexCount))
            throw std::runtime_error("Error: Index data read failed");


        // Buffers creation
        Buffer vertexBuffer = Buffer(m_device, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Buffer indexBuffer = Buffer(m_device, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);


        // Staging buffers creation
        const Buffer vertexStagingBuffer = Buffer(m_device, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
        const Buffer indexStagingBuffer = Buffer(m_device, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);


        // Transfers to staging buffers
        void *data = nullptr;
        vertexStagingBuffer.map(&data);
        memcpy(data, vertices.data(), static_cast<size_t>(vertices.size()) * sizeof(Vertex));
        vertexStagingBuffer.unmap();

        indexStagingBuffer.map(&data);
        memcpy(data, indices.data(), static_cast<size_t>(indices.size()) * sizeof(uint32_t));
        indexStagingBuffer.unmap();


        // Transfers to gpu buffers
        VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::Graphics); {
            vertexBuffer.copyFrom(commandBuffer, vertexStagingBuffer.getHandle(), vertices.size() * sizeof(Vertex));
            indexBuffer.copyFrom(commandBuffer, indexStagingBuffer.getHandle(), indices.size() * sizeof(uint32_t));
        } m_device->endSingleTimeCommands(Device::Graphics, commandBuffer);


        // Acceleration structure get sizes
        const VkAccelerationStructureGeometryKHR accelerationStructureGeometry{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = {
                .triangles = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                    .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                    .vertexData = {
                        .deviceAddress = vertexBuffer.getDeviceAddress(),
                    },
                    .vertexStride = sizeof(Vertex),
                    .maxVertex = static_cast<uint32_t>(vertices.size()),
                    .indexType = VK_INDEX_TYPE_UINT32,
                    .indexData = {
                        .deviceAddress = indexBuffer.getDeviceAddress(),
                    },
                },
            },
            .flags = static_cast<VkGeometryFlagsKHR>(static_cast<fastgltf::AlphaMode>(m_materials[materialIndex].alphaMode) == fastgltf::AlphaMode::Opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR),
        };

        const VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR,
            .geometryCount = 1,
            .pGeometries = &accelerationStructureGeometry,
        };

        const uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;
        VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        };

        vkGetAccelerationStructureBuildSizesKHR(m_device->getHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationStructureBuildGeometryInfo, &numTriangles, &accelerationStructureBuildSizesInfo);


        // Acceleration structure creation
        Buffer accelerationStructureBuffer = Buffer(m_device, accelerationStructureBuildSizesInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        VkAccelerationStructureKHR originAccelerationStructure = VK_NULL_HANDLE;

        const VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = accelerationStructureBuffer.getHandle(),
            .size = accelerationStructureBuildSizesInfo.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        };
        VK_CHECK(vkCreateAccelerationStructureKHR(m_device->getHandle(), &accelerationStructureCreateInfo, nullptr, &originAccelerationStructure));


        // Acceleration structure build
        const Buffer scratchBuffer = Buffer(m_device, accelerationStructureBuildSizesInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        const VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .dstAccelerationStructure = originAccelerationStructure,
            .geometryCount = 1,
            .pGeometries = &accelerationStructureGeometry,
            .scratchData {
                .deviceAddress = scratchBuffer.getDeviceAddress(),
            },
        };

        VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{
            .primitiveCount = numTriangles,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0,
        };
        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

        commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
            vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationBuildGeometryInfo, accelerationBuildStructureRangeInfos.data());
        } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);


        // Query compacted size
        const VkQueryPoolCreateInfo queryPoolCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
            .queryCount = 1,
        };
        VkQueryPool queryPool = VK_NULL_HANDLE;
        VK_CHECK(vkCreateQueryPool(m_device->getHandle(), &queryPoolCreateInfo, nullptr, &queryPool));

        commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
            vkCmdResetQueryPool(commandBuffer, queryPool, 0, 1);
            vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, 1, &originAccelerationStructure, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queryPool, 0);
        } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);

        VkDeviceSize compactedSize = 0;
        VK_CHECK(vkGetQueryPoolResults(m_device->getHandle(), queryPool, 0, 1, sizeof(VkDeviceSize), &compactedSize, sizeof(VkDeviceSize), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        vkDestroyQueryPool(m_device->getHandle(), queryPool, nullptr);


        // Compacted acceleration structure creation
        Buffer compactedAccelerationStructureBuffer = Buffer(m_device, compactedSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        VkAccelerationStructureKHR compactedAccelerationStructure = VK_NULL_HANDLE;
        const VkAccelerationStructureCreateInfoKHR compactedAccelerationStructureCreateInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = compactedAccelerationStructureBuffer.getHandle(),
            .size = compactedSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        };
        VK_CHECK(vkCreateAccelerationStructureKHR(m_device->getHandle(), &compactedAccelerationStructureCreateInfo, nullptr, &compactedAccelerationStructure));

        commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
            const VkCopyAccelerationStructureInfoKHR copyAccelerationStructureInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
                .src = originAccelerationStructure,
                .dst = compactedAccelerationStructure,
                .mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR,
            };

            vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyAccelerationStructureInfo);
        } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);


        // Original acceleration structure cleanup
        vkDestroyAccelerationStructureKHR(m_device->getHandle(), originAccelerationStructure, nullptr);


        // Get acceleration structure device address
        const VkAccelerationStructureDeviceAddressInfoKHR compactedAccelerationDeviceAddressInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .accelerationStructure = compactedAccelerationStructure,
        };
        const VkDeviceAddress compactedAccelerationStructureAddress = vkGetAccelerationStructureDeviceAddressKHR(m_device->getHandle(), &compactedAccelerationDeviceAddressInfo);


        // New mesh creation
        m_meshes[i] = std::make_shared<Mesh>(Mesh{
            .vertexBuffer = std::move(vertexBuffer),
            .indexBuffer = std::move(indexBuffer),
            .indexCount = static_cast<uint32_t>(indices.size()),
            .accelerationStructure = AccelerationStructure{
                .handle = compactedAccelerationStructure,
                .deviceAddress = compactedAccelerationStructureAddress,
                .buffer = std::move(compactedAccelerationStructureBuffer),
            },
            .materialIndex = static_cast<int>(materialIndex),
        });
    }
}

void Viewer::loadMeshInstances(std::ifstream& file) {
    std::vector<KelpMeshInstance> kelpMeshInstances;
    std::vector<MeshInstance> meshInstances;

    // Read mesh instance count
    size_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    meshInstances.reserve(count);
    m_accelerationStructureInstances.reserve(count);
    kelpMeshInstances.resize(count);


    // Read mesh instance data
    file.read(reinterpret_cast<char*>(kelpMeshInstances.data()), static_cast<std::streamsize>(sizeof(KelpMeshInstance) * count));


    // Convert to acceleration structure instances
    for (const auto& meshInstance : kelpMeshInstances) {
        const VkTransformMatrixKHR transformMatrix = {
            .matrix = {
                {meshInstance.transform[0][0], meshInstance.transform[1][0], meshInstance.transform[2][0], meshInstance.transform[3][0]},
                {meshInstance.transform[0][1], meshInstance.transform[1][1], meshInstance.transform[2][1], meshInstance.transform[3][1]},
                {meshInstance.transform[0][2], meshInstance.transform[1][2], meshInstance.transform[2][2], meshInstance.transform[3][2]}
            },
        };

        const VkAccelerationStructureInstanceKHR instance{
            .transform = transformMatrix,
            .instanceCustomIndex = static_cast<uint32_t>(meshInstances.size()),
            .mask = 0xFF,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
            .accelerationStructureReference = m_meshes[meshInstance.meshIndex]->accelerationStructure.deviceAddress,
        };
        m_accelerationStructureInstances.push_back(instance);

        meshInstances.push_back(MeshInstance{
            .vertexBuffer = m_meshes[meshInstance.meshIndex]->vertexBuffer.getDeviceAddress(),
            .indexBuffer = m_meshes[meshInstance.meshIndex]->indexBuffer.getDeviceAddress(),
            .materialIndex = m_meshes[meshInstance.meshIndex]->materialIndex,
        });
    }


    // Primitive instances buffer creation
    const Buffer primitiveInstancesStagingBuffer = Buffer(m_device, meshInstances.size() * sizeof(MeshInstance), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    void *data = nullptr;
    primitiveInstancesStagingBuffer.map(&data);
    memcpy(data, meshInstances.data(), meshInstances.size() * sizeof(MeshInstance));
    primitiveInstancesStagingBuffer.unmap();

    m_meshInstanceBuffer = std::make_unique<Buffer>(m_device, meshInstances.size() * sizeof(MeshInstance), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
        m_meshInstanceBuffer->copyFrom(commandBuffer, primitiveInstancesStagingBuffer.getHandle(), meshInstances.size() * sizeof(MeshInstance));
    } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);


    // BLAS instance array creation
    const Buffer instancesBuffer = Buffer(m_device, m_accelerationStructureInstances.size() * sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    data = nullptr;
    instancesBuffer.map(&data);
    memcpy(data, m_accelerationStructureInstances.data(), m_accelerationStructureInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));
    instancesBuffer.unmap();


    // TLAS get sizes
    const VkAccelerationStructureGeometryKHR accelerationStructureGeometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry = {
            .instances = {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                .arrayOfPointers = VK_FALSE,
                .data = {
                    .deviceAddress = instancesBuffer.getDeviceAddress(),
                },
            },
        },
    };

    const VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .geometryCount = 1,
        .pGeometries = &accelerationStructureGeometry,
    };

    const uint32_t numInstances = static_cast<uint32_t>(m_accelerationStructureInstances.size());

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vkGetAccelerationStructureBuildSizesKHR(m_device->getHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationStructureBuildGeometryInfo, &numInstances, &accelerationStructureBuildSizesInfo);


    // TLAS creation
    m_topLevelAccelerationStructureBuffer = std::make_unique<Buffer>(m_device, accelerationStructureBuildSizesInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

    const VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = m_topLevelAccelerationStructureBuffer->getHandle(),
        .size = accelerationStructureBuildSizesInfo.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    };
    VK_CHECK(vkCreateAccelerationStructureKHR(m_device->getHandle(), &accelerationStructureCreateInfo, nullptr, &m_topLevelAccelerationStructure));


    // TLAS build
    const Buffer scratchBuffer = Buffer(m_device, accelerationStructureBuildSizesInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

    const VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .dstAccelerationStructure = m_topLevelAccelerationStructure,
        .geometryCount = 1,
        .pGeometries = &accelerationStructureGeometry,
        .scratchData {
            .deviceAddress = scratchBuffer.getDeviceAddress(),
        },
    };

    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{
        .primitiveCount = numInstances,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0,
    };

    commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
        const std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };
        vkCmdBuildAccelerationStructuresKHR(
            commandBuffer,
            1,
            &accelerationBuildGeometryInfo,
            accelerationBuildStructureRangeInfos.data());
    } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);

    m_descriptorManager.storeAccelerationStructure(m_topLevelAccelerationStructure);
}

void Viewer::loadAssetsFromFile(const std::filesystem::path& filePath) {
    if (!std::filesystem::exists(filePath))
        throw std::runtime_error("Error loading \"" + filePath.string() + "\": file not found");

    // Create sampler
    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = m_device->getProperties().limits.maxSamplerAnisotropy,
        .minLod = 0,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    VK_CHECK(vkCreateSampler(m_device->getHandle(), &samplerInfo, nullptr, &m_defaultSampler));

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Error opening \"" + filePath.string() + "\": file not found");


    // Read textures
    std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();

    loadAndUploadTextureCollection(filePath, file, m_albedoTextures, VK_FORMAT_R8G8B8A8_UNORM, 4);
    loadAndUploadTextureCollection(filePath, file, m_alphaTextures, VK_FORMAT_R8_UNORM, 1);
    loadAndUploadTextureCollection(filePath, file, m_normalTextures, VK_FORMAT_R8G8B8A8_UNORM, 4);
    loadAndUploadTextureCollection(filePath, file, m_metallicRoughnessTextures, VK_FORMAT_R8G8_UNORM, 2);
    loadAndUploadTextureCollection(filePath, file, m_emissiveTextures, VK_FORMAT_R8G8B8A8_UNORM, 4);

    std::chrono::high_resolution_clock::time_point endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedTime = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
    std::cout << "Texture loading took " << elapsedTime.count() << " seconds" << std::endl;

    std::cout << "Textures loaded: " << m_albedoTextures.size() << " albedo, " << m_alphaTextures.size() << " alpha, "
              << m_normalTextures.size() << " normal, " << m_metallicRoughnessTextures.size() << " metallic-roughness, "
              << m_emissiveTextures.size() << " emissive" << std::endl;


    // Read materials
    startTime = std::chrono::high_resolution_clock::now();

    loadMaterials(file);

    endTime = std::chrono::high_resolution_clock::now();
    elapsedTime = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
    std::cout << "Loaded "<< m_materials.size() << " materials in " << elapsedTime.count() << " seconds" << std::endl;


    // Read meshes
    startTime = std::chrono::high_resolution_clock::now();

    loadMeshes(file);

    endTime = std::chrono::high_resolution_clock::now();
    elapsedTime = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
    std::cout << "Loaded "<< m_meshes.size() << " meshes in " << elapsedTime.count() << " seconds" << std::endl;


    // Read mesh instances
    startTime = std::chrono::high_resolution_clock::now();

    loadMeshInstances(file);

    endTime = std::chrono::high_resolution_clock::now();
    elapsedTime = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
    std::cout << "Loaded "<< m_accelerationStructureInstances.size() << " mesh instances in " << elapsedTime.count() << " seconds" << std::endl;
}
