#pragma once

#include "glm/ext/vector_int2.hpp"
#include "omm.hpp"
#include "shared.hpp"

#include "fastgltf/types.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

struct Texture {
    glm::ivec2 size;
    int gltfIndex;
    std::vector<uint8_t> data;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int materialIndex;
    int ommIndex;
    int gltfIndex;
};

struct KelpMeshInstance {
    glm::mat4 transform;
    int meshIndex;
};

class Converter {
    public:
        Converter() = default;
        ~Converter() = default;

        Converter(const Converter&) = delete;
        Converter& operator=(const Converter&) = delete;

        Converter(Converter&&) = delete;
        Converter& operator=(Converter&&) = delete;

        void convert(const std::filesystem::path& inputFile, const std::filesystem::path& outputFile);

    private:
        static fastgltf::Asset parseFile(const std::filesystem::path& inputFile);
        static void funcTime(const std::string& context, const std::function<void()>& func);

        void loadMaterials(const fastgltf::Asset& asset);
        static int processTextureIndex(int originalIndex, std::vector<Texture>& textureCollection);
        void initTextureCollections();
        void loadTextures(fastgltf::Asset& asset, const std::filesystem::path& inputFile);
        static std::pair<glm::ivec2, uint8_t*> loadTexture(fastgltf::Asset& asset, const std::filesystem::path& inputFile, const fastgltf::Texture& gltfTexture, int desiredChannels);
        void bakeOpacityMicromaps();
        void loadMeshes(fastgltf::Asset& asset);
        void loadGltfScene(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Scene& scene);
        void loadGltfNode(const std::filesystem::path& filePath, const fastgltf::Asset& asset, const fastgltf::Node& node, const glm::mat4& parentTransform = glm::mat4(1));
        void concatenateTextures();

        std::vector<Mesh> m_meshes;
        std::vector<KelpMeshInstance> m_meshInstances;
        omm::Cpu::SerializedResult m_serializedOmms = nullptr;

        std::vector<Material> m_materials;

        std::vector<Texture> m_finalTextures;

        std::vector<Texture> m_albedoTextures;
        std::vector<Texture> m_alphaTextures;
        std::vector<Texture> m_normalTextures;
        std::vector<Texture> m_metallicRoughnessTextures;
        std::vector<Texture> m_emissiveTextures;
};
