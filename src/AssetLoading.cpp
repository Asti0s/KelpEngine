#include "App.hpp"

#include "Vulkan/Buffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Utils.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define GLM_ENABLE_EXPERIMENTAL
#include "fastgltf/core.hpp"
#include "fastgltf/math.hpp"
#include "fastgltf/tools.hpp"
#include "fastgltf/types.hpp"
#include "fastgltf/util.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
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
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

void App::loadMaterials(const fastgltf::Asset& asset) {
    std::vector<GpuMaterial> gpuMaterials;
    gpuMaterials.reserve(asset.materials.size());

    for (const auto& material : asset.materials) {
        const GpuMaterial gpuMaterial{
            .baseColorTextureIndex = material.pbrData.baseColorTexture.has_value() ? static_cast<int>(material.pbrData.baseColorTexture.value().textureIndex) : -1,
            .normalTextureIndex = material.normalTexture.has_value() ? static_cast<int>(material.normalTexture.value().textureIndex) : -1,
            .metallicRoughnessTextureIndex = material.pbrData.metallicRoughnessTexture.has_value() ? static_cast<int>(material.pbrData.metallicRoughnessTexture.value().textureIndex) : -1,
            .emissiveTextureIndex = material.emissiveTexture.has_value() ? static_cast<int>(material.emissiveTexture.value().textureIndex) : -1,
            .baseColorFactor = glm::vec4(material.pbrData.baseColorFactor.x(), material.pbrData.baseColorFactor.y(), material.pbrData.baseColorFactor.z(), material.pbrData.baseColorFactor.w()),
            .metallicFactor = material.pbrData.metallicFactor,
            .roughnessFactor = material.pbrData.roughnessFactor,
            .emissiveFactor = glm::vec3(material.emissiveFactor.x(), material.emissiveFactor.y(), material.emissiveFactor.z()),
            .alphaMode = static_cast<int>(material.alphaMode),
            .alphaCutoff = material.alphaCutoff,
        };

        gpuMaterials.push_back(gpuMaterial);
    }

    const Buffer gpuMaterialStagingBuffer = Buffer(m_device, gpuMaterials.size() * sizeof(GpuMaterial), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    void *data = nullptr;
    gpuMaterialStagingBuffer.map(&data);
    memcpy(data, gpuMaterials.data(), gpuMaterials.size() * sizeof(GpuMaterial));
    gpuMaterialStagingBuffer.unmap();

    m_gpuMaterials = std::make_unique<Buffer>(m_device, gpuMaterials.size() * sizeof(GpuMaterial), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::Graphics); {
        m_gpuMaterials->copyFrom(commandBuffer, gpuMaterialStagingBuffer.getHandle(), gpuMaterials.size() * sizeof(GpuMaterial));
    } m_device->endSingleTimeCommands(Device::Graphics, commandBuffer);
}

void App::loadSamplers(const fastgltf::Asset& asset) {
    m_samplers.reserve(asset.samplers.size());

    static constexpr auto gltfToVkFilter = [](std::optional<fastgltf::Filter> filter) -> VkFilter {
        if (!filter.has_value())
            return VK_FILTER_NEAREST;

        switch (filter.value()) {
            case fastgltf::Filter::Linear:
            case fastgltf::Filter::LinearMipMapNearest:
            case fastgltf::Filter::LinearMipMapLinear:
            default:
                return VK_FILTER_LINEAR;
            case fastgltf::Filter::Nearest:
            case fastgltf::Filter::NearestMipMapNearest:
            case fastgltf::Filter::NearestMipMapLinear:
                return VK_FILTER_NEAREST;
        }
    };

    static constexpr auto gltfToVkMipmapMode = [](std::optional<fastgltf::Filter> filter) -> VkSamplerMipmapMode {
        if (!filter.has_value())
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;

        switch (filter.value()) {
            case fastgltf::Filter::Linear:
            case fastgltf::Filter::NearestMipMapLinear:
            case fastgltf::Filter::LinearMipMapLinear:
            default:
                return VK_SAMPLER_MIPMAP_MODE_LINEAR;
            case fastgltf::Filter::Nearest:
            case fastgltf::Filter::NearestMipMapNearest:
            case fastgltf::Filter::LinearMipMapNearest:
                return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        }
    };

    static constexpr auto gltfToVkSamplerAddressMode = [](fastgltf::Wrap wrap) -> VkSamplerAddressMode {
        switch (wrap) {
            case fastgltf::Wrap::ClampToEdge:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case fastgltf::Wrap::MirroredRepeat:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case fastgltf::Wrap::Repeat:
            default:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    };

    for (const auto& sampler : asset.samplers) {
        const VkSamplerCreateInfo samplerCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = gltfToVkFilter(sampler.magFilter),
            .minFilter = gltfToVkFilter(sampler.minFilter),
            .mipmapMode = gltfToVkMipmapMode(sampler.minFilter),
            .addressModeU = gltfToVkSamplerAddressMode(sampler.wrapS),
            .addressModeV = gltfToVkSamplerAddressMode(sampler.wrapT),
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias = 0,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = m_device->getProperties().limits.maxSamplerAnisotropy,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0,
            .maxLod = VK_LOD_CLAMP_NONE,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        VkSampler vkSampler = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSampler(m_device->getHandle(), &samplerCreateInfo, nullptr, &vkSampler));

        m_samplers.push_back(vkSampler);
    }
}

void App::loadTextures(fastgltf::Asset& asset) {
    m_textures.reserve(asset.textures.size());

    for (const fastgltf::Texture& texture : asset.textures) {
        std::shared_ptr<Image>& image = m_images[texture.imageIndex.value()];
        VkSampler& sampler = m_samplers[texture.samplerIndex.value()];

        m_textures.push_back(Texture {
            .image = image,
            .sampler = sampler,
            .bindlessId = m_descriptorManager.storeSampledImage(image->getImageView(), sampler),
        });
    }
}

Image App::loadImage(uint8_t *data, const glm::ivec2& size, std::mutex& commandMutex) {
    // Image creation
    const Image::CreateInfo imageCreateInfo{
        .extent = VkExtent3D{static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), 1},
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .type = VK_IMAGE_TYPE_2D,
        .mipLevels = static_cast<uint8_t>(std::floor(std::log2(std::max(size.x, size.y))) + 1),
    };
    Image image = Image(m_device, imageCreateInfo);


    // Staging buffer creation and mapping with texture data
    Buffer stagingBuffer = Buffer(m_device, static_cast<size_t>(size.x * size.y * 4), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    void *mappedData = nullptr;
    stagingBuffer.map(&mappedData);
    memcpy(mappedData, data, static_cast<int>(size.x * size.y * 4));
    stagingBuffer.unmap();


    // Transfer from staging buffer to image and generate mipmaps
    commandMutex.lock();
    VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
        image.cmdTransitionLayout(commandBuffer, Image::Layout{
            .layout = VK_IMAGE_LAYOUT_UNDEFINED,
            .accessMask = 0,
            .stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        }, Image::Layout{
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .accessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
        });

        image.cmdCopyFromBuffer(commandBuffer, stagingBuffer.getHandle());
        image.cmdGenerateMipmaps(commandBuffer, Image::Layout{
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .accessMask = VK_ACCESS_SHADER_READ_BIT,
            .stageFlags = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        });
    } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);
    commandMutex.unlock();


    // Cleanup
    stbi_image_free(data);

    return std::move(image);
}

void App::loadImages(const std::filesystem::path& filePath, fastgltf::Asset& asset) {
    std::vector<std::thread> loadingThreads(asset.images.size());
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    std::mutex commandMutex;
    std::mutex pushMutex;

    for (uint32_t i = 0; i < asset.images.size(); i++) {
        loadingThreads[i] = std::thread([&, i]() {
            fastgltf::Image& image = asset.images[i];

            glm::ivec2 size;
            int nrChannels = 0;

            std::shared_ptr<Image> newImage = std::make_unique<Image>(std::visit(fastgltf::visitor {
                [](auto& /* UNUSED */) -> Image {
                    throw std::runtime_error("Failed to load image: unknown image type");
                },
                [&](fastgltf::sources::BufferView& imageBufferView) -> Image {
                    const fastgltf::BufferView& bufferView = asset.bufferViews[imageBufferView.bufferViewIndex];
                    fastgltf::Buffer& buffer = asset.buffers[bufferView.bufferIndex];

                    return std::visit(fastgltf::visitor {
                        [](auto& /* UNUSED */) -> Image {
                            throw std::runtime_error("Failed to load image buffer view: unknown buffer type");
                        },
                        [&](fastgltf::sources::Array& array) -> Image {
                            uint8_t *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(array.bytes.data() + bufferView.byteOffset), static_cast<int>(bufferView.byteLength), &size.x, &size.y, &nrChannels, STBI_rgb_alpha);
                            if(data == nullptr)
                                throw std::runtime_error("Error loading image bytes: " + std::string(stbi_failure_reason()));

                            return loadImage(data, size, commandMutex);
                        }
                    }, buffer.data);
                },
                [&](fastgltf::sources::Array& array) -> Image {
                    uint8_t *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(array.bytes.data()), static_cast<int>(array.bytes.size()), &size.x, &size.y, &nrChannels, STBI_rgb_alpha);
                    if(data == nullptr)
                        throw std::runtime_error("Error loading image bytes: " + std::string(stbi_failure_reason()));

                    return loadImage(data, size, commandMutex);
                },
                [&](fastgltf::sources::URI& texturePath) -> Image {
                    const std::filesystem::path path = std::filesystem::path(filePath).parent_path().append(texturePath.uri.c_str());
                    if (!std::filesystem::exists(path))
                        throw std::runtime_error("Error loading \"" + path.string() + "\": file not found");

                    uint8_t *data = stbi_load(path.string().c_str(), &size.x, &size.y, &nrChannels, STBI_rgb_alpha);
                    if (data == nullptr)
                        throw std::runtime_error("Failed to load texture \"" + path.string() + "\": " + std::string(stbi_failure_reason()));

                    return loadImage(data, size, commandMutex);
                },
            }, image.data));

            std::clog << "Loaded image " << i << std::endl;
            pushMutex.lock();
            m_images[i] = std::move(newImage);
            pushMutex.unlock();
        });
    }

    for (auto& thread : loadingThreads)
        if (thread.joinable())
            thread.join();

    const std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    const float elapsedTime = std::chrono::duration<float>(end - start).count();
    std::clog << "Loaded all images in " << elapsedTime << " seconds" << std::endl;
}

void App::loadMeshes(fastgltf::Asset& asset) {
    for (const auto& mesh : asset.meshes) {
        std::shared_ptr<Mesh> newMesh = std::make_shared<Mesh>();
        newMesh->primitives.reserve(mesh.primitives.size());

        for (const auto& primitive : mesh.primitives)
            newMesh->primitives.push_back(loadPrimitive(asset, primitive));

        m_meshes.push_back(std::move(newMesh));
    }
}

App::Primitive App::loadPrimitive(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive) {
    // Vertices loading
    if (primitive.findAttribute("POSITION") == nullptr)
        throw std::runtime_error("Failed to load primitive: missing POSITION attribute");

    if (primitive.findAttribute("TEXCOORD_0") == nullptr)
        throw std::runtime_error("Failed to load primitive: missing TEXCOORD_0 attribute");

    if (primitive.findAttribute("NORMAL") == nullptr)
        throw std::runtime_error("Failed to load primitive: missing NORMAL attribute");

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

    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, uvAccessor, [&](const fastgltf::math::fvec2& value, size_t index) {
        vertices[index].uv = {value.x(), value.y()};
    });

    fastgltf::iterateAccessorWithIndex<uint32_t>(asset, indicesAccessor, [&](const uint32_t& value, size_t index) {
        indices[index] = value;
    });


    // Buffers creation
    Buffer vertexBuffer = Buffer(m_device, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    Buffer indexBuffer = Buffer(m_device, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);


    // Staging buffers creation
    const Buffer vertexStagingBuffer = Buffer(m_device, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    const Buffer indexStagingBuffer = Buffer(m_device, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);


    // Transfer to staging buffers
    void *data = nullptr;
    vertexStagingBuffer.map(&data);
    memcpy(data, vertices.data(), vertices.size() * sizeof(Vertex));
    vertexStagingBuffer.unmap();

    indexStagingBuffer.map(&data);
    memcpy(data, indices.data(), indices.size() * sizeof(uint32_t));
    indexStagingBuffer.unmap();


    // Transfer from staging buffers to buffers
    VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
        vertexBuffer.copyFrom(commandBuffer, vertexStagingBuffer.getHandle(), vertices.size() * sizeof(Vertex));
        indexBuffer.copyFrom(commandBuffer, indexStagingBuffer.getHandle(), indices.size() * sizeof(uint32_t));
    } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);


    // Get material
    const fastgltf::Material& material = asset.materials.at(primitive.materialIndex.value());
    std::cout << (material.alphaMode == fastgltf::AlphaMode::Opaque ? "Opaque" : "Transparent") << std::endl;


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
        .flags = static_cast<VkGeometryFlagsKHR>(material.alphaMode == fastgltf::AlphaMode::Opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR),
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

    vkGetAccelerationStructureBuildSizesKHR(
        m_device->getHandle(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo,
        &numTriangles,
        &accelerationStructureBuildSizesInfo);


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
        vkCmdBuildAccelerationStructuresKHR(
            commandBuffer,
            1,
            &accelerationBuildGeometryInfo,
            accelerationBuildStructureRangeInfos.data());
    } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);


    // Query compacted size
    VkQueryPoolCreateInfo queryPoolCreateInfo = {
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

    std::cout << "Original size: " << static_cast<float>(accelerationStructureBuildSizesInfo.accelerationStructureSize) / 1024 << " KB" << std::endl;
    std::cout << "Compacted size: " << static_cast<float>(compactedSize) / 1024 << " KB" << std::endl;
    std::cout << "Compaction ratio: " << static_cast<float>(compactedSize) / static_cast<float>(accelerationStructureBuildSizesInfo.accelerationStructureSize) * 100 << " %" << std::endl;
    static float totalGained = 0;
    totalGained += static_cast<float>(accelerationStructureBuildSizesInfo.accelerationStructureSize - compactedSize) / 1024 / 1024;
    std::cout << "Total gained: " << totalGained << " MB" << std::endl << std::endl;


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
    Primitive newPrimitive {
        .vertexBuffer = std::move(vertexBuffer),
        .indexBuffer = std::move(indexBuffer),
        .indexCount = static_cast<uint32_t>(indices.size()),
        .accelerationStructure = {
            .handle = compactedAccelerationStructure,
            .deviceAddress = compactedAccelerationStructureAddress,
            .buffer = std::move(compactedAccelerationStructureBuffer),
        },
        .materialIndex = static_cast<int>(primitive.materialIndex.value()),
    };

    return newPrimitive;
}

void App::loadGltfNode(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Node& node, const glm::mat4& parentTransform) {
    const glm::mat4 localTransform = std::visit(fastgltf::visitor {
        [&](const fastgltf::math::fmat4x4& matrix) -> glm::mat4 {
            return parentTransform * glm::make_mat4x4(matrix.data());
        },
        [&](const fastgltf::TRS& transform) -> glm::mat4 {
            return parentTransform
                * glm::translate(glm::mat4(1), {transform.translation.x(), transform.translation.y(), transform.translation.z()})
                * glm::mat4_cast(glm::quat(transform.rotation.w(), transform.rotation.x(), transform.rotation.y(), transform.rotation.z()))
                * glm::scale(glm::mat4(1), {transform.scale.x(), transform.scale.y(), transform.scale.z()});
        },
        [&](const auto& /* UNUSED */) -> glm::mat4 {
            throw std::runtime_error("Failed to load node: unknown node type");
        }
    }, node.transform);

    const VkTransformMatrixKHR transformMatrix = {
        .matrix = {
            {localTransform[0][0], localTransform[1][0], localTransform[2][0], localTransform[3][0]},
            {localTransform[0][1], localTransform[1][1], localTransform[2][1], localTransform[3][1]},
            {localTransform[0][2], localTransform[1][2], localTransform[2][2], localTransform[3][2]}
        },
    };

    if (node.meshIndex.has_value()) {
        const std::shared_ptr<Mesh>& mesh = m_meshes.at(node.meshIndex.value());

        MeshInstance newMeshInstance {
            .mesh = mesh,
        };

        for (const auto& primitive : mesh->primitives) {
            const VkAccelerationStructureInstanceKHR instance{
                .transform = transformMatrix,
                .instanceCustomIndex = static_cast<uint32_t>(m_gpuPrimitiveInstances.size()),
                .mask = 0xFF,
                .instanceShaderBindingTableRecordOffset = 0,
                .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
                .accelerationStructureReference = primitive.accelerationStructure.deviceAddress,
            };
            newMeshInstance.instances.push_back(instance);

            m_gpuPrimitiveInstances.push_back(GpuPrimitiveInstance {
                .vertexBufferAddress = primitive.vertexBuffer.getDeviceAddress(),
                .indexBufferAddress = primitive.indexBuffer.getDeviceAddress(),
                .materialIndex = primitive.materialIndex,
            });
        }

        m_meshInstances.push_back(newMeshInstance);
    }

    for (const size_t childIndex : node.children) {
        const fastgltf::Node& childNode = asset.nodes.at(childIndex);
        loadGltfNode(filePath, asset, childNode, localTransform);
    }
}

void App::loadGltfScene(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Scene& scene) {
    for (const size_t nodeIndice : scene.nodeIndices) {
        const fastgltf::Node& node = asset.nodes.at(nodeIndice);
        loadGltfNode(filePath, asset, node);
    }


    // Gpu primitive instances buffer creation
    const Buffer gpuPrimitiveInstancesStagingBuffer = Buffer(m_device, m_gpuPrimitiveInstances.size() * sizeof(GpuPrimitiveInstance), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    void *data = nullptr;
    gpuPrimitiveInstancesStagingBuffer.map(&data);
    memcpy(data, m_gpuPrimitiveInstances.data(), m_gpuPrimitiveInstances.size() * sizeof(GpuPrimitiveInstance));
    gpuPrimitiveInstancesStagingBuffer.unmap();

    m_gpuPrimitiveInstancesBuffer = std::make_unique<Buffer>(m_device, m_gpuPrimitiveInstances.size() * sizeof(GpuPrimitiveInstance), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
        m_gpuPrimitiveInstancesBuffer->copyFrom(commandBuffer, gpuPrimitiveInstancesStagingBuffer.getHandle(), m_gpuPrimitiveInstances.size() * sizeof(GpuPrimitiveInstance));
    } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);


    // BLAS instance array creation
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    for (const MeshInstance& meshInstance : m_meshInstances)
        for (const VkAccelerationStructureInstanceKHR& instance : meshInstance.instances)
            instances.push_back(instance);

    const Buffer instancesBuffer = Buffer(m_device, instances.size() * sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    data = nullptr;
    instancesBuffer.map(&data);
    memcpy(data, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
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

    const uint32_t numInstances = static_cast<uint32_t>(instances.size());

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vkGetAccelerationStructureBuildSizesKHR(
        m_device->getHandle(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo,
        &numInstances,
        &accelerationStructureBuildSizesInfo);


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

    loadSamplers(asset);
    loadImages(path, asset);
    loadTextures(asset);
    loadMaterials(asset);
    loadMeshes(asset);
    loadGltfScene(filePath, asset, asset.scenes[0]);
}
