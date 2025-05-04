#ifdef __cplusplus
    #pragma once

    #include <glm/glm.hpp>
    #include <vulkan/vulkan_core.h>

    using vec3 = glm::vec3;
    using vec2 = glm::vec2;
    using vec4 = glm::vec4;
    using mat4 = glm::mat4;
#endif

#define STORAGE_IMAGE_BINDING 0
#define COMBINED_IMAGE_SAMPLER_BINDING 1
#define ACCELERATION_STRUCTURE_BINDING 2

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
};

struct Material {
    // Textures
    int baseColorTexture;
    int alphaTexture;
    int normalTexture;
    int metallicRoughnessTexture;
    int emissiveTexture;

    // Factors
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    vec3 emissiveFactor;

    // Params
    int alphaMode;
    float alphaCutoff;
};

#ifndef __cplusplus
    layout(buffer_reference, scalar) buffer VertexBuffer { Vertex vertices[]; };
    layout(buffer_reference, scalar) buffer IndexBuffer { uint indices[]; };
#endif

struct MeshInstance {
    #ifdef __cplusplus
        VkDeviceAddress vertexBuffer;
        VkDeviceAddress indexBuffer;
    #else
        VertexBuffer vertexBuffer;
        IndexBuffer indexBuffer;
    #endif
    int materialIndex;
};

#ifndef __cplusplus
    layout(buffer_reference, scalar) buffer Materials { Material materials[]; };
    layout(buffer_reference, scalar) buffer MeshInstances { MeshInstance meshInstances[]; };
#endif

struct PushConstant {
    mat4 inverseView;
    mat4 inverseProjection;
    #ifdef __cplusplus
        VkDeviceAddress meshInstanceBuffer;
        VkDeviceAddress materialsBuffer;
    #else
        MeshInstances meshInstanceBuffer;
        Materials materialBuffer;
    #endif
};
