#include "Converter/Converter.hpp"
#include "shared.hpp"

#include "fastgltf/core.hpp"
#include "fastgltf/tools.hpp"
#include "fastgltf/types.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "omm.hpp"
#include "stb_image.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <thread>

void Converter::funcTime(const std::string& context, const std::function<void()>& func) {
    const auto timeNow = std::chrono::high_resolution_clock::now();
    func();
    const auto timeEnd = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(timeEnd - timeNow).count();
    std::cout << context << " in " << duration << " ms" << std::endl;
}

fastgltf::Asset Converter::parseFile(const std::filesystem::path& inputFile) {
    if (!std::filesystem::exists(inputFile))
        throw std::runtime_error("Input file does not exist: " + inputFile.string());

    fastgltf::Expected<fastgltf::GltfDataBuffer> dataBuffer = fastgltf::GltfDataBuffer::FromPath(inputFile);
    if (dataBuffer.error() != fastgltf::Error::None)
        throw std::runtime_error("Failed to load \"" + inputFile.string() + "\": " + std::string(fastgltf::getErrorName(dataBuffer.error())) + ": " + std::string(fastgltf::getErrorMessage(dataBuffer.error())));

    constexpr fastgltf::Options options =
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::AllowDouble |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::GenerateMeshIndices;

    fastgltf::Parser parser;
    if (inputFile.extension() == ".gltf") {
        fastgltf::Expected<fastgltf::Asset> expectedAsset = parser.loadGltf(dataBuffer.get(), inputFile.parent_path(), options);
        if (expectedAsset.error() != fastgltf::Error::None)
            throw std::runtime_error("Failed to load \"" + inputFile.string() + "\": " + std::string(fastgltf::getErrorName(expectedAsset.error())) + ": " + std::string(fastgltf::getErrorMessage(expectedAsset.error())));

        return std::move(expectedAsset.get());
    }

    if (inputFile.extension() == ".glb") {
        fastgltf::Expected<fastgltf::Asset> expectedAsset = parser.loadGltfBinary(dataBuffer.get(), inputFile.parent_path(), options);
        if (expectedAsset.error() != fastgltf::Error::None)
            throw std::runtime_error("Failed to load \"" + inputFile.string() + "\": " + std::string(fastgltf::getErrorName(expectedAsset.error())) + ": " + std::string(fastgltf::getErrorMessage(expectedAsset.error())));

        return std::move(expectedAsset.get());
    }

    throw std::runtime_error("Failed to load \"" + inputFile.string() + "\": unknown file extension");
}

void Converter::loadMaterials(const fastgltf::Asset& asset) {
    m_materials.reserve(asset.materials.size());

    for (const auto& gltfMaterial : asset.materials) {
        const Material material{
            .baseColorTexture = gltfMaterial.pbrData.baseColorTexture.has_value() ? static_cast<int>(gltfMaterial.pbrData.baseColorTexture.value().textureIndex) : -1,
            .alphaTexture = -1,
            .normalTexture = gltfMaterial.normalTexture.has_value() ? static_cast<int>(gltfMaterial.normalTexture.value().textureIndex) : -1,
            .metallicRoughnessTexture = gltfMaterial.pbrData.metallicRoughnessTexture.has_value() ? static_cast<int>(gltfMaterial.pbrData.metallicRoughnessTexture.value().textureIndex) : -1,
            .emissiveTexture = gltfMaterial.emissiveTexture.has_value() ? static_cast<int>(gltfMaterial.emissiveTexture.value().textureIndex) : -1,
            .baseColorFactor = glm::vec4(gltfMaterial.pbrData.baseColorFactor.x(), gltfMaterial.pbrData.baseColorFactor.y(), gltfMaterial.pbrData.baseColorFactor.z(), gltfMaterial.pbrData.baseColorFactor.w()),
            .metallicFactor = gltfMaterial.pbrData.metallicFactor,
            .roughnessFactor = gltfMaterial.pbrData.roughnessFactor,
            .emissiveFactor = glm::vec3(gltfMaterial.emissiveFactor.x(), gltfMaterial.emissiveFactor.y(), gltfMaterial.emissiveFactor.z()),
            .alphaMode = static_cast<int>(gltfMaterial.alphaMode),
            .alphaCutoff = gltfMaterial.alphaCutoff
        };

        m_materials.push_back(material);
    }
}

int Converter::processTextureIndex(int originalIndex, std::vector<Texture>& textureCollection) {
    if (originalIndex == -1)
        return -1;

    // Find if a texture with the same glTF index already exists in the collection
    auto it = std::ranges::find_if(textureCollection,
        [originalIndex](const Texture& texture) {
            return texture.gltfIndex == originalIndex;
        });

    // If not, create a new texture and add it to the collection
    if (it == textureCollection.end()) {
        Texture texture;
        texture.gltfIndex = originalIndex;
        textureCollection.push_back(texture);
        return static_cast<int>(textureCollection.size() - 1);
    }

    // Else, return the index of the existing texture
    return static_cast<int>(std::distance(textureCollection.begin(), it));
}

void Converter::generateMipmaps(Texture& texture, int channels) {
    const MipLevel* previousMip = texture.mipLevels.data();

    while (previousMip->size.x > 16 || previousMip->size.y > 16) {
        MipLevel nextMip{
            .size = {
                std::max(16, previousMip->size.x / 2),
                std::max(16, previousMip->size.y / 2)
            },
        };

        if (static_cast<size_t>(nextMip.size.x) * static_cast<size_t>(nextMip.size.y) * static_cast<size_t>(channels) == 0 && (nextMip.size.x > 0 || nextMip.size.y > 0))
            break;

        nextMip.data.resize(static_cast<size_t>(nextMip.size.x) * static_cast<size_t>(nextMip.size.y) * static_cast<size_t>(channels));

        for (int y = 0; y < nextMip.size.y; ++y) {
            for (int x = 0; x < nextMip.size.x; ++x) {
                for (int c = 0; c < channels; ++c) {
                    uint32_t sum = 0;
                    int count = 0;

                    for (int offsetY = 0; offsetY < 2; ++offsetY) {
                        int prevY = (y * 2) + offsetY;
                        if (prevY >= previousMip->size.y)
                            continue;

                        for (int offsetX = 0; offsetX < 2; ++offsetX) {
                            int prevX = (x * 2) + offsetX;
                            if (prevX >= previousMip->size.x)
                                continue;

                            sum += previousMip->data[(static_cast<size_t>((prevY * previousMip->size.x) + prevX) * channels) + c];
                            count++;
                        }
                    }

                    nextMip.data[(static_cast<size_t>((y * nextMip.size.x) + x) * channels) + c] = static_cast<uint8_t>(sum / count);
                }
            }
        }

        texture.mipLevels.emplace_back(std::move(nextMip));
        previousMip = &texture.mipLevels.back();
    }
}

void Converter::initTextureCollections() {
    // Add all textures to their respective collections and update materials to switch from glTF indices to our own indices
    for (auto& material : m_materials) {
        // Create a new alpha texture if the material is not opaque
        if (material.alphaMode != static_cast<int>(fastgltf::AlphaMode::Opaque) && material.baseColorTexture != -1)
            material.alphaTexture = processTextureIndex(material.baseColorTexture, m_alphaTextures);

        material.baseColorTexture = processTextureIndex(material.baseColorTexture, m_albedoTextures);
        material.metallicRoughnessTexture = processTextureIndex(material.metallicRoughnessTexture, m_metallicRoughnessTextures);
        material.normalTexture = processTextureIndex(material.normalTexture, m_normalTextures);
        material.emissiveTexture = processTextureIndex(material.emissiveTexture, m_emissiveTextures);
    }
}

std::pair<glm::ivec2, uint8_t*> Converter::loadTexture(fastgltf::Asset& asset, const std::filesystem::path& inputFile, const fastgltf::Texture& gltfTexture, int desiredChannels) {
    if (!gltfTexture.imageIndex.has_value())
        throw std::runtime_error("Unsupported texture format: no image index found for texture");

    fastgltf::Image& image = asset.images[gltfTexture.imageIndex.value()];
    glm::ivec2 size;

    uint8_t *data = std::visit(fastgltf::visitor {
        [](auto& /* UNUSED */) -> uint8_t * {
            throw std::runtime_error("Failed to load image: unknown image type");
        },
        [&](fastgltf::sources::BufferView& imageBufferView) -> uint8_t * {
            const fastgltf::BufferView& bufferView = asset.bufferViews[imageBufferView.bufferViewIndex];
            fastgltf::Buffer& buffer = asset.buffers[bufferView.bufferIndex];

            return std::visit(fastgltf::visitor {
                [](auto& /* UNUSED */) -> uint8_t * {
                    throw std::runtime_error("Failed to load image buffer view: unknown buffer type");
                },
                [&](fastgltf::sources::Array& array) -> uint8_t * {
                    return stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(array.bytes.data() + bufferView.byteOffset), static_cast<int>(bufferView.byteLength), &size.x, &size.y, nullptr, desiredChannels);
                }
            }, buffer.data);
        },
        [&](fastgltf::sources::Array& array) -> uint8_t * {
            return stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(array.bytes.data()), static_cast<int>(array.bytes.size()), &size.x, &size.y, nullptr, desiredChannels);
        },
        [&](fastgltf::sources::URI& texturePath) -> uint8_t * {
            const std::filesystem::path path = std::filesystem::path(inputFile).parent_path().append(texturePath.uri.c_str());
            if (!std::filesystem::exists(path))
                throw std::runtime_error("Error loading \"" + path.string() + "\": file not found");

            return stbi_load(path.string().c_str(), &size.x, &size.y, nullptr, desiredChannels);
        },
    }, image.data);

    if (data == nullptr)
        throw std::runtime_error("Failed to load image: " + std::string(stbi_failure_reason()));

    return { size, data };
}

void Converter::loadTextures(fastgltf::Asset& asset, const std::filesystem::path& inputFile) {
    std::vector<std::thread> threads;
    threads.reserve(m_albedoTextures.size() + m_alphaTextures.size() + m_normalTextures.size() + m_metallicRoughnessTextures.size() + m_emissiveTextures.size());

    // Process albedo textures (RGBA format)
    for (Texture& albedoTexture : m_albedoTextures) {
        threads.emplace_back([&]() {
            const fastgltf::Texture& gltfTexture = asset.textures[albedoTexture.gltfIndex];
            const auto [size, data] = loadTexture(asset, inputFile, gltfTexture, STBI_rgb_alpha);

            // First mip level creation from loaded data
            albedoTexture.mipLevels.emplace_back(MipLevel{
                .size = size,
                .data = std::vector<uint8_t>(static_cast<size_t>(size.x * size.y) * 4),
            });
            std::copy(data, data + static_cast<ptrdiff_t>(static_cast<size_t>(size.x * size.y) * 4), albedoTexture.mipLevels[0].data.begin());

            // Generate mipmaps & clean up
            generateMipmaps(albedoTexture, 4);
            stbi_image_free(data);
        });
    }

    // Wait for albedo texture threads to finish before processing alpha textures since they depend on them
    for (auto& thread : threads) {
        if (thread.joinable())
            thread.join();
    }

    // Process alpha textures (extract alpha channel from albedo)
    for (Texture& alphaTexture : m_alphaTextures) {
        threads.emplace_back([&]() {
            const auto it = std::ranges::find_if(m_albedoTextures,
                [&alphaTexture](const Texture& texture) {
                    return texture.gltfIndex == alphaTexture.gltfIndex;
                });

            // Texture first mip level creation from albedo texture
            const MipLevel& albedoTexture = it->mipLevels[0];
            alphaTexture.mipLevels.emplace_back(MipLevel{
                .size = albedoTexture.size,
                .data = std::vector<uint8_t>(static_cast<size_t>(albedoTexture.size.x * albedoTexture.size.y)),
            });

            // Extracting alpha channel from albedo texture
            for (int y = 0; y < alphaTexture.mipLevels[0].size.y; ++y) {
                for (int x = 0; x < alphaTexture.mipLevels[0].size.x; ++x) {
                    const size_t index = (static_cast<size_t>(y) * alphaTexture.mipLevels[0].size.x) + x;
                    alphaTexture.mipLevels[0].data[index] = albedoTexture.data[(index * 4) + 3];
                }
            }

            // Generate mipmaps
            generateMipmaps(alphaTexture, 1);
        });
    }

    // Process normal textures
    for (Texture& normalTexture : m_normalTextures) {
        threads.emplace_back([&]() {
            const fastgltf::Texture& gltfTexture = asset.textures[normalTexture.gltfIndex];
            const auto [size, data] = loadTexture(asset, inputFile, gltfTexture, STBI_rgb_alpha);

            // First mip level creation from loaded data
            normalTexture.mipLevels.emplace_back(MipLevel{
                .size = size,
                .data = std::vector<uint8_t>(static_cast<size_t>(size.x * size.y) * 4),
            });
            std::copy(data, data + static_cast<ptrdiff_t>(static_cast<size_t>(size.x * size.y) * 4), normalTexture.mipLevels[0].data.begin());

            // Generate mipmaps & clean up
            stbi_image_free(data);
            generateMipmaps(normalTexture, 4);
        });
    }

    // Process metallic-roughness textures (encode to 2-channel format)
    for (Texture& metallicRoughnessTexture : m_metallicRoughnessTextures) {
        threads.emplace_back([&]() {
            const fastgltf::Texture& gltfTexture = asset.textures[metallicRoughnessTexture.gltfIndex];
            const auto [size, data] = loadTexture(asset, inputFile, gltfTexture, STBI_rgb);

            // Texture first mip level creation
            metallicRoughnessTexture.mipLevels.emplace_back(MipLevel{
                .size = size,
                .data = std::vector<uint8_t>(static_cast<size_t>(size.x * size.y) * 2),
            });

            // Extracting metallic and roughness channels from loaded data
            for (int y = 0; y < size.y; ++y) {
                for (int x = 0; x < size.x; ++x) {
                    const size_t index = (static_cast<size_t>(y) * size.x) + x;
                    const size_t srcIndex = index * 3;
                    const size_t encodedIndex = index * 2;

                    metallicRoughnessTexture.mipLevels[0].data[encodedIndex] = data[srcIndex];
                    metallicRoughnessTexture.mipLevels[0].data[encodedIndex + 1] = data[srcIndex + 1];
                }
            }

            // Generate mipmaps & clean up
            generateMipmaps(metallicRoughnessTexture, 2);
            stbi_image_free(data);
        });
    }

    // Process emissive textures
    for (Texture& emissiveTexture : m_emissiveTextures) {
        threads.emplace_back([&]() {
            const fastgltf::Texture& gltfTexture = asset.textures[emissiveTexture.gltfIndex];
            const auto [size, data] = loadTexture(asset, inputFile, gltfTexture, STBI_rgb_alpha);

            // Texture first mip level creation
            emissiveTexture.mipLevels.emplace_back(MipLevel{
                .size = size,
                .data = std::vector<uint8_t>(static_cast<size_t>(size.x * size.y) * 4),
            });
            std::copy(data, data + static_cast<ptrdiff_t>(static_cast<size_t>(size.x * size.y) * 4), emissiveTexture.mipLevels[0].data.begin());

            // Generate mipmaps & clean up
            generateMipmaps(emissiveTexture, 4);
            stbi_image_free(data);
        });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
        if (thread.joinable())
            thread.join();
    }
}

void Converter::loadMeshes(fastgltf::Asset& asset) {
    for (uint32_t i = 0; i < asset.meshes.size(); i++) {
        const fastgltf::Mesh& gltfMesh = asset.meshes[i];

        for (const auto& primitive : gltfMesh.primitives) {
            if (!primitive.materialIndex.has_value())
                throw std::runtime_error("Failed to load primitive: missing material index");

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

            m_meshes.push_back(Mesh{
                .vertices = std::move(vertices),
                .indices = std::move(indices),
                .materialIndex = static_cast<int>(primitive.materialIndex.value()),
                .ommIndex = -1,
                .gltfIndex = static_cast<int>(i),
            });
        }
    }
}

void Converter::bakeOpacityMicromaps() {
    // Baker creation
    const omm::BakerCreationDesc desc {
        .type = omm::BakerType::CPU,
    };

    omm::Baker bakerHandle = nullptr;
    omm::Result res = omm::CreateBaker(desc, &bakerHandle);
    if (res != omm::Result::SUCCESS)
        throw std::runtime_error("Failed to create OMM baker: " + std::to_string(static_cast<int>(res)));


    // OMM resources
    std::vector<omm::Cpu::BakeResultDesc> bakeResultDescs;  // Used for serialization
    std::vector<omm::Cpu::BakeResult> bakeResults;          // We have to keep them for destroying after serialization


    for (auto& mesh: m_meshes) {
        Material& material = m_materials.at(mesh.materialIndex);

        if (material.alphaMode != static_cast<int>(fastgltf::AlphaMode::Opaque) && material.alphaTexture != -1) {
            const Texture& alphaTexture = m_alphaTextures[material.alphaTexture];

            // Separation of texCoords from vertices
            std::vector<glm::vec2> texCoordBuffer(mesh.vertices.size());
            for (size_t i = 0; i < mesh.vertices.size(); i++)
                texCoordBuffer[i] = mesh.vertices[i].uv;


            // OMM texture creation
            std::vector<omm::Cpu::TextureMipDesc> mipDescs(alphaTexture.mipLevels.size());
            for (size_t i = 0; i < alphaTexture.mipLevels.size(); i++) {
                mipDescs[i] = {
                    .width = static_cast<uint32_t>(alphaTexture.mipLevels[i].size.x),
                    .height = static_cast<uint32_t>(alphaTexture.mipLevels[i].size.y),
                    .textureData = alphaTexture.mipLevels[i].data.data(),
                };
            }

            const omm::Cpu::TextureDesc texDesc {
                .format = omm::Cpu::TextureFormat::UNORM8,
                .mips = mipDescs.data(),
                .mipCount = static_cast<uint32_t>(mipDescs.size()),
                .alphaCutoff = material.alphaCutoff,
            };

            omm::Cpu::Texture textureHandle = nullptr;
            res = omm::Cpu::CreateTexture(bakerHandle, texDesc, &textureHandle);
            if (res != omm::Result::SUCCESS)
                throw std::runtime_error("Failed to create OMM texture: " + std::to_string(static_cast<int>(res)));


            // OMM baking
            const omm::Cpu::BakeInputDesc bakeDesc {
                .bakeFlags = omm::Cpu::BakeFlags::EnableInternalThreads,
                .texture = textureHandle,
                .runtimeSamplerDesc = { .addressingMode = omm::TextureAddressMode::Mirror, .filter = omm::TextureFilterMode::Linear },
                .alphaMode = material.alphaMode == static_cast<int>(fastgltf::AlphaMode::Mask) ? omm::AlphaMode::Test : omm::AlphaMode::Blend,
                .texCoordFormat = omm::TexCoordFormat::UV32_FLOAT,
                .texCoords = texCoordBuffer.data(),
                .texCoordStrideInBytes = sizeof(glm::vec2),
                .indexFormat = omm::IndexFormat::UINT_32,
                .indexBuffer = mesh.indices.data(),
                .indexCount = static_cast<uint32_t>(mesh.indices.size()),
                .alphaCutoff = material.alphaCutoff,
                .format = omm::Format::OC1_4_State,
                .unknownStatePromotion = omm::UnknownStatePromotion::ForceOpaque,
            };

            omm::Cpu::BakeResult bakeResultHandle = nullptr;
            res = omm::Cpu::Bake(bakerHandle, bakeDesc, &bakeResultHandle);
            if (res != omm::Result::SUCCESS)
                throw std::runtime_error("Failed to bake OMM: " + std::to_string(static_cast<int>(res)));

            const omm::Cpu::BakeResultDesc* bakeResultDesc = nullptr;
            res = omm::Cpu::GetBakeResultDesc(bakeResultHandle, &bakeResultDesc);
            if (res != omm::Result::SUCCESS)
                throw std::runtime_error("Failed to get OMM bake result: " + std::to_string(static_cast<int>(res)));


            // Adding the micromap to the list for serialization or destroying it if the material is finally opaque
            if (bakeResultDesc->arrayDataSize == 0) {
                material.alphaMode = static_cast<int>(fastgltf::AlphaMode::Opaque);
                res = omm::Cpu::DestroyBakeResult(bakeResultHandle);
            } else {
                mesh.ommIndex = static_cast<int>(bakeResultDescs.size());
                bakeResultDescs.emplace_back(*bakeResultDesc);
                bakeResults.emplace_back(bakeResultHandle);
            }


            // Clean up
            res = omm::Cpu::DestroyTexture(bakerHandle, textureHandle);
            if (res != omm::Result::SUCCESS)
                throw std::runtime_error("Failed to destroy OMM texture: " + std::to_string(static_cast<int>(res)));
        }
    }


    // OMM Serialization
    const omm::Cpu::DeserializedDesc deserializedDesc{
        .numResultDescs = static_cast<int>(bakeResultDescs.size()),
        .resultDescs = bakeResultDescs.data(),
    };

    res = omm::Cpu::Serialize(bakerHandle, deserializedDesc, &m_serializedOmms);
    if (res != omm::Result::SUCCESS)
        throw std::runtime_error("Failed to serialize OMM: " + std::to_string(static_cast<int>(res)));


    // Clean up (destroying bake results and baker)
    for (const auto& bakeResult : bakeResults) {
        res = omm::Cpu::DestroyBakeResult(bakeResult);
        if (res != omm::Result::SUCCESS)
            throw std::runtime_error("Failed to destroy OMM bake result: " + std::to_string(static_cast<int>(res)));
    }

    res = omm::DestroyBaker(bakerHandle);
    if (res != omm::Result::SUCCESS)
        throw std::runtime_error("Failed to destroy OMM baker: " + std::to_string(static_cast<int>(res)));
}

void Converter::loadGltfNode(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Node& node, const glm::mat4& parentTransform) {
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

    if (node.meshIndex.has_value()) {
        for (int i = 0; i < m_meshes.size(); i++) {
            if (node.meshIndex.value() == m_meshes[i].gltfIndex) {
                m_meshInstances.push_back(KelpMeshInstance{
                    .transform = localTransform,
                    .meshIndex = i
                });
            }
        }
    }

    for (const size_t childIndex : node.children) {
        const fastgltf::Node& childNode = asset.nodes.at(childIndex);
        loadGltfNode(filePath, asset, childNode, localTransform);
    }
}

void Converter::loadGltfScene(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Scene& scene) {
    for (const size_t nodeIndice : scene.nodeIndices) {
        const fastgltf::Node& node = asset.nodes.at(nodeIndice);
        loadGltfNode(filePath, asset, node);
    }
}

void Converter::convert(const std::filesystem::path& inputFile, const std::filesystem::path& outputFile) {
    fastgltf::Asset asset;

    funcTime("Converted file", [&]() {
        funcTime("Parsed file", [&]() {
            asset = parseFile(inputFile);
        });

        funcTime("Loaded materials", [&]() {
            loadMaterials(asset);
        });

        funcTime("Loaded textures", [&]() {
            initTextureCollections();
            loadTextures(asset, inputFile);
        });

        funcTime("Loaded meshes", [&]() {
            loadMeshes(asset);
        });

        funcTime("Baked opacity micromaps", [&]() {
            bakeOpacityMicromaps();
        });

        funcTime("Loaded glTF scene", [&]() {
            loadGltfScene(inputFile, asset, asset.scenes[0]);
        });
    });


    // Opening file to write in
    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile.is_open())
        throw std::runtime_error("Failed to open output file: " + outputFile.string());


    // Writing textures
    const size_t albedoCount = m_albedoTextures.size();
    outFile.write(reinterpret_cast<const char*>(&albedoCount), sizeof(size_t));
    for (const Texture& albedoTexture : m_albedoTextures) {
        const size_t mipLevelCount = albedoTexture.mipLevels.size();
        outFile.write(reinterpret_cast<const char*>(&mipLevelCount), sizeof(size_t));

        for (size_t i = 0; i < mipLevelCount; ++i) {
            const MipLevel& mipLevel = albedoTexture.mipLevels[i];
            outFile.write(reinterpret_cast<const char*>(&mipLevel.size), sizeof(glm::ivec2));
            outFile.write(reinterpret_cast<const char*>(mipLevel.data.data()), static_cast<std::streamsize>(sizeof(uint8_t) * mipLevel.data.size()));
        }
    }

    const size_t alphaCount = m_alphaTextures.size();
    outFile.write(reinterpret_cast<const char*>(&alphaCount), sizeof(size_t));
    for (const Texture& alphaTexture : m_alphaTextures) {
        const size_t mipLevelCount = alphaTexture.mipLevels.size();
        outFile.write(reinterpret_cast<const char*>(&mipLevelCount), sizeof(size_t));

        for (size_t i = 0; i < mipLevelCount; ++i) {
            const MipLevel& mipLevel = alphaTexture.mipLevels[i];
            outFile.write(reinterpret_cast<const char*>(&mipLevel.size), sizeof(glm::ivec2));
            outFile.write(reinterpret_cast<const char*>(mipLevel.data.data()), static_cast<std::streamsize>(sizeof(uint8_t) * mipLevel.data.size()));
        }
    }

    const size_t normalCount = m_normalTextures.size();
    outFile.write(reinterpret_cast<const char*>(&normalCount), sizeof(size_t));
    for (const Texture& normalTexture : m_normalTextures) {
        const size_t mipLevelCount = normalTexture.mipLevels.size();
        outFile.write(reinterpret_cast<const char*>(&mipLevelCount), sizeof(size_t));

        for (size_t i = 0; i < mipLevelCount; ++i) {
            const MipLevel& mipLevel = normalTexture.mipLevels[i];
            outFile.write(reinterpret_cast<const char*>(&mipLevel.size), sizeof(glm::ivec2));
            outFile.write(reinterpret_cast<const char*>(mipLevel.data.data()), static_cast<std::streamsize>(sizeof(uint8_t) * mipLevel.data.size()));
        }
    }

    const size_t metallicRoughnessCount = m_metallicRoughnessTextures.size();
    outFile.write(reinterpret_cast<const char*>(&metallicRoughnessCount), sizeof(size_t));
    for (const Texture& metallicRoughnessTexture : m_metallicRoughnessTextures) {
        const size_t mipLevelCount = metallicRoughnessTexture.mipLevels.size();
        outFile.write(reinterpret_cast<const char*>(&mipLevelCount), sizeof(size_t));

        for (size_t i = 0; i < mipLevelCount; ++i) {
            const MipLevel& mipLevel = metallicRoughnessTexture.mipLevels[i];
            outFile.write(reinterpret_cast<const char*>(&mipLevel.size), sizeof(glm::ivec2));
            outFile.write(reinterpret_cast<const char*>(mipLevel.data.data()), static_cast<std::streamsize>(sizeof(uint8_t) * mipLevel.data.size()));
        }
    }

    const size_t emissiveCount = m_emissiveTextures.size();
    outFile.write(reinterpret_cast<const char*>(&emissiveCount), sizeof(size_t));
    for (const Texture& emissiveTexture : m_emissiveTextures) {
        const size_t mipLevelCount = emissiveTexture.mipLevels.size();
        outFile.write(reinterpret_cast<const char*>(&mipLevelCount), sizeof(size_t));

        for (size_t i = 0; i < mipLevelCount; ++i) {
            const MipLevel& mipLevel = emissiveTexture.mipLevels[i];
            outFile.write(reinterpret_cast<const char*>(&mipLevel.size), sizeof(glm::ivec2));
            outFile.write(reinterpret_cast<const char*>(mipLevel.data.data()), static_cast<std::streamsize>(sizeof(uint8_t) * mipLevel.data.size()));
        }
    }


    // Writing materials
    const size_t materialCount = m_materials.size();
    outFile.write(reinterpret_cast<const char*>(&materialCount), sizeof(size_t));
    outFile.write(reinterpret_cast<const char*>(m_materials.data()), static_cast<std::streamsize>(sizeof(Material) * materialCount));


    // Writing OMMs
    const omm::Cpu::BlobDesc* blobDesc = nullptr;
    omm::Result res = omm::Cpu::GetSerializedResultDesc(m_serializedOmms, &blobDesc);
    if (res != omm::Result::SUCCESS)
        throw std::runtime_error("Failed to get serialized OMM result desc: " + std::to_string(static_cast<int>(res)));

    const size_t blobSize = blobDesc->size;
    outFile.write(reinterpret_cast<const char*>(&blobSize), sizeof(size_t));
    outFile.write(reinterpret_cast<const char*>(blobDesc->data), static_cast<std::streamsize>(sizeof(uint8_t) * blobSize));

    res = omm::Cpu::DestroySerializedResult(m_serializedOmms);
    if (res != omm::Result::SUCCESS)
        throw std::runtime_error("Failed to destroy serialized OMM result: " + std::to_string(static_cast<int>(res)));


    // Writing meshes
    const size_t meshCount = m_meshes.size();
    outFile.write(reinterpret_cast<const char*>(&meshCount), sizeof(size_t));
    for (const Mesh& mesh : m_meshes) {
        const size_t materialIndex = mesh.materialIndex;
        outFile.write(reinterpret_cast<const char*>(&materialIndex), sizeof(size_t));

        const int ommIndex = mesh.ommIndex;
        outFile.write(reinterpret_cast<const char*>(&ommIndex), sizeof(int));

        const size_t vertexCount = mesh.vertices.size();
        outFile.write(reinterpret_cast<const char*>(&vertexCount), sizeof(size_t));
        outFile.write(reinterpret_cast<const char*>(mesh.vertices.data()), static_cast<std::streamsize>(sizeof(Vertex) * vertexCount));

        const size_t indexCount = mesh.indices.size();
        outFile.write(reinterpret_cast<const char*>(&indexCount), sizeof(size_t));
        outFile.write(reinterpret_cast<const char*>(mesh.indices.data()), static_cast<std::streamsize>(sizeof(uint32_t) * indexCount));
    }


    // Write mesh instances
    const size_t meshInstanceCount = m_meshInstances.size();
    outFile.write(reinterpret_cast<const char*>(&meshInstanceCount), sizeof(size_t));
    outFile.write(reinterpret_cast<const char*>(m_meshInstances.data()), static_cast<std::streamsize>(sizeof(KelpMeshInstance) * meshInstanceCount));


    outFile.close();
    std::cout << "Conversion completed successfully!" << std::endl;
}
