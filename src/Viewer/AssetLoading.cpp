#include "Viewer/Viewer.hpp"

#include "Viewer/Vulkan/Buffer.hpp"
#include "Viewer/Vulkan/Device.hpp"
#include "Viewer/Vulkan/Image.hpp"
#include "Viewer/Vulkan/Utils.hpp"

#define GLM_ENABLE_EXPERIMENTAL
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

void Viewer::loadAndUploadTextureCollection(const std::filesystem::path& filePath, std::ifstream& file, std::vector<Texture>& targetCollection, VkFormat textureFormat, int channelCount) {
    // Read texture count
    size_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    targetCollection.resize(count);


    // Read texture metadata (size and offset in the file) to allow for concurrent reading instead
    std::vector<TextureMetaData> textureMetadata(count);

    for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
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
        .maxAnisotropy = m_device->getProperties().limits.maxSamplerAnisotropy,
        .minLod = 0,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    VK_CHECK(vkCreateSampler(m_device->getHandle(), &samplerInfo, nullptr, &m_defaultSampler));


    // Read from file
    std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Error opening \"" + filePath.string() + "\": file not found");

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
}
