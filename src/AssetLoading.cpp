#include "App.hpp"
#include "Vulkan/VkBuffer.hpp"
#include "Vulkan/VkDevice.hpp"
#include "Vulkan/VkUtils.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define GLM_ENABLE_EXPERIMENTAL
#include "fastgltf/core.hpp"
#include "fastgltf/tools.hpp"
#include "fastgltf/types.hpp"
#include "flecs/addons/cpp/entity.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/string_cast.hpp"
#include "stb_image.h"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan_core.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

void App::loadPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive, const std::string& name) {
    // Vertices loading
    if (primitive.findAttribute("POSITION") == nullptr)
        return;

    if (primitive.findAttribute("TEXCOORD_0") == nullptr)
        return;

    if (primitive.findAttribute("NORMAL") == nullptr)
        return;

    const fastgltf::Accessor& positionAccessor = asset.accessors.at(primitive.findAttribute("POSITION")->accessorIndex);
    const fastgltf::Accessor& normalAccessor = asset.accessors.at(primitive.findAttribute("NORMAL")->accessorIndex);
    const fastgltf::Accessor& uvAccessor = asset.accessors.at(primitive.findAttribute("TEXCOORD_0")->accessorIndex);
    const fastgltf::Accessor& indicesAccessor = asset.accessors.at(primitive.indicesAccessor.value());

    std::vector<Vertex> vertices(positionAccessor.count);
    std::vector<uint32_t> indices(indicesAccessor.count);

    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, positionAccessor, [&](const fastgltf::math::fvec3& value, size_t index) {
        vertices[index].position = {value.x(), value.y(), value.z()};
    });

    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, normalAccessor, [&](const fastgltf::math::fvec3& value, size_t index) {
        vertices[index].normal = {value.x(), value.y(), value.z()};
    });

    fastgltf::iterateAccessorWithIndex<uint32_t>(asset, indicesAccessor, [&](const uint32_t& value, size_t index) {
        indices[index] = value;
    });


    // Mesh creation
    Vk::Buffer vertexBuffer = Vk::Buffer(m_device, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkDeviceAddress vertexBufferDeviceAddress = vertexBuffer.getDeviceAddress();

    Vk::Buffer indexBuffer = Vk::Buffer(m_device, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkDeviceAddress indexBufferDeviceAddress = indexBuffer.getDeviceAddress();

    Vk::Buffer transformBuffer = Vk::Buffer(m_device, sizeof(glm::mat4), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    VkDeviceAddress transformBufferDeviceAddress = transformBuffer.getDeviceAddress();

    const VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_device->getGraphicsCommandPool(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer{};
    vkAllocateCommandBuffers(m_device->getHandle(), &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    Vk::Buffer vertexStagingBuffer = Vk::Buffer(m_device, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    void *data = nullptr;
    vertexStagingBuffer.map(&data);
    memcpy(data, vertices.data(), vertices.size() * sizeof(Vertex));
    vertexStagingBuffer.unmap();
    vertexBuffer.copyFrom(commandBuffer, vertexStagingBuffer.getHandle(), vertices.size() * sizeof(Vertex));

    Vk::Buffer indexStagingBuffer = Vk::Buffer(m_device, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    indexStagingBuffer.map(&data);
    memcpy(data, indices.data(), indices.size() * sizeof(uint32_t));
    indexStagingBuffer.unmap();
    indexBuffer.copyFrom(commandBuffer, indexStagingBuffer.getHandle(), indices.size() * sizeof(uint32_t));

    glm::mat4 transform = glm::mat4(1);
    transformBuffer.map(&data);
    memcpy(data, glm::value_ptr(transform), sizeof(glm::mat4));
    transformBuffer.unmap();



    // Acceleration structure get sizes
    const VkAccelerationStructureGeometryKHR accelerationStructureGeometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry = {
            .triangles = {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                .vertexData = {
                    .deviceAddress = vertexBufferDeviceAddress,
                },
                .vertexStride = sizeof(Vertex),
                .maxVertex = static_cast<uint32_t>(vertices.size()),
                .indexType = VK_INDEX_TYPE_UINT32,
                .indexData = {
                    .deviceAddress = indexBufferDeviceAddress,
                },
                .transformData = {
                    .deviceAddress = transformBufferDeviceAddress,
                },
            },
        },
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
    };

    const VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .geometryCount = 1,
        .pGeometries = &accelerationStructureGeometry,
    };

    const uint32_t numTriangles = static_cast<uint32_t>(indices.size()) / 3;
    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };

    vkGetAccelerationStructureBuildSizesKHR(
        m_device->getHandle(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo,
        &numTriangles,
        &accelerationStructureBuildSizesInfo);




    // Acceleration structure creation
    Vk::Buffer accelerationStructureBuffer = Vk::Buffer(m_device, accelerationStructureBuildSizesInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
    VkAccelerationStructureKHR accelerationStructureHandle = VK_NULL_HANDLE;

    const VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = accelerationStructureBuffer.getHandle(),
        .size = accelerationStructureBuildSizesInfo.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    };
    VK_CHECK(vkCreateAccelerationStructureKHR(m_device->getHandle(), &accelerationStructureCreateInfo, nullptr, &accelerationStructureHandle));




    // Acceleration structure build
    Vk::Buffer scratchBuffer = Vk::Buffer(m_device, accelerationStructureBuildSizesInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

    const VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .dstAccelerationStructure = accelerationStructureHandle,
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

    vkCmdBuildAccelerationStructuresKHR(
        commandBuffer,
        1,
        &accelerationBuildGeometryInfo,
        accelerationBuildStructureRangeInfos.data());




    // Get acceleration structure device address
    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = accelerationStructureHandle,
    };
    VkDeviceAddress accelerationStructureAddress = vkGetAccelerationStructureDeviceAddressKHR(m_device->getHandle(), &accelerationDeviceAddressInfo);




    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };

    vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->getGraphicsQueue());

    Mesh newMesh {
        .vertexBuffer = std::move(vertexBuffer),
        .vertexBufferDeviceAddress = vertexBufferDeviceAddress,
        .indexBuffer = std::move(indexBuffer),
        .indexBufferDeviceAddress = indexBufferDeviceAddress,
        .indexCount = static_cast<uint32_t>(indices.size()),
        .accelerationStructure = {
            .handle = accelerationStructureHandle,
            .deviceAddress = accelerationStructureAddress,
            .buffer = std::move(accelerationStructureBuffer),
        },
    };

    m_meshes.insert({name, std::move(newMesh)});
}

void App::loadGltfNode(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Node& node, const glm::mat4& parentTransform) {
    glm::mat4 localTransform = std::visit(fastgltf::visitor {
        [&](const fastgltf::math::fmat4x4& matrix) -> glm::mat4 {
            return glm::make_mat4x4(matrix.data()) * parentTransform;
        },
        [&](const fastgltf::TRS& transform) -> glm::mat4 {
            return glm::translate(glm::mat4(1), {transform.translation.x(), transform.translation.y(), transform.translation.z()})
                * glm::mat4_cast(glm::quat(transform.rotation.x(), transform.rotation.y(), transform.rotation.z(), transform.rotation.w()))
                * glm::scale(glm::mat4(1), {transform.scale.x(), transform.scale.y(), transform.scale.z()})
                * parentTransform;
        },
        [&](const auto& /* UNUSED */) -> glm::mat4 {
            throw std::runtime_error("Failed to load node: unknown node type");
        }
    }, node.transform);

    if (node.meshIndex.has_value()) {
        const fastgltf::Mesh& mesh = asset.meshes.at(node.meshIndex.value());

        for (int i = 0; i < mesh.primitives.size(); i++) {
            const fastgltf::Primitive& primitive = mesh.primitives[i];

            const std::string meshName = filePath.filename().replace_extension("").string() + "_mesh" + std::to_string(node.meshIndex.value()) + "_primitive" + std::to_string(i);
            if (m_meshes.find(meshName) == m_meshes.end()) {
                loadPrimitive(asset, primitive, meshName);
            } else {
                std::clog << "Mesh \"" << meshName << "\" already loaded" << std::endl;
            }

            if (!m_meshes.contains(meshName)) {
                std::clog << "Warning: failed to load mesh \"" << meshName << "\"" << std::endl;
                continue;
            }

            Mesh& newMesh = m_meshes.at(meshName);

            const flecs::entity entity = m_ecs.entity()
                .insert([&](MeshComponent& meshComponent) {
                    meshComponent = {
                        .vertexBuffer = &newMesh.vertexBuffer,
                        .indexBuffer = &newMesh.indexBuffer,
                        .indexCount = newMesh.indexCount,
                        .transform = localTransform,
                    };
                });
        }
    }

    for (size_t childIndex : node.children) {
        const fastgltf::Node& childNode = asset.nodes.at(childIndex);
        loadGltfNode(filePath, asset, childNode, localTransform);
    }
}

void App::loadGltfScene(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Scene& scene) {
    for (unsigned long long nodeIndice : scene.nodeIndices) {
        const fastgltf::Node& node = asset.nodes.at(nodeIndice);
        loadGltfNode(filePath, asset, node);
    }
}

void App::loadAssetsFromFile(const char *filePath) {
    const std::filesystem::path path = filePath;
    if (!std::filesystem::exists(path))
        throw std::runtime_error("Error loading \"" + path.string() + "\": file not found");

    fastgltf::Expected<fastgltf::GltfDataBuffer> dataBuffer = fastgltf::GltfDataBuffer::FromPath(path);
    if (dataBuffer.error() != fastgltf::Error::None)
        throw std::runtime_error("Error loading \"" + path.string() + "\": " + std::string(fastgltf::getErrorName(dataBuffer.error())) + ": " + std::string(fastgltf::getErrorMessage(dataBuffer.error())));

    constexpr fastgltf::Options options =
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::AllowDouble |
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::GenerateMeshIndices;

    fastgltf::Parser parser;
    fastgltf::Asset asset;
    if (path.extension() == ".gltf") {
        fastgltf::Expected<fastgltf::Asset> expectedAsset = parser.loadGltf(dataBuffer.get(), path.parent_path(), options);
        if (expectedAsset.error() != fastgltf::Error::None)
            throw std::runtime_error("Error loading \"" + path.string() + "\": " + std::string(fastgltf::getErrorName(expectedAsset.error())) + ": " + std::string(fastgltf::getErrorMessage(expectedAsset.error())));

        asset = std::move(expectedAsset.get());
    } else if (path.extension() == ".glb") {
        fastgltf::Expected<fastgltf::Asset> expectedAsset = parser.loadGltfBinary(dataBuffer.get(), path.parent_path(), options);
        if (expectedAsset.error() != fastgltf::Error::None)
            throw std::runtime_error("Error loading \"" + path.string() + "\": " + std::string(fastgltf::getErrorName(expectedAsset.error())) + ": " + std::string(fastgltf::getErrorMessage(expectedAsset.error())));

        asset = std::move(expectedAsset.get());
    } else {
        throw std::runtime_error("Error loading \"" + path.string() + "\": unknown file extension");
    }

    for (const auto& scene : asset.scenes)
        loadGltfScene(filePath, asset, scene);
}
