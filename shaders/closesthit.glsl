#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

struct Material {
    // Textures
    int baseColorTexture;
    int normalTexture;
    int metallicTexture;
    int roughnessTexture;

    // Factors
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    vec3 emissiveFactor;

    // Params
    int alphaMode;
    float alphaCutoff;
};

layout(buffer_reference, scalar) buffer Materials {
    Material materials[];
};

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
};

layout(buffer_reference, scalar) buffer Vertices {
    Vertex vertices[];
};

layout(buffer_reference, scalar) buffer Indices {
    uint indices[];
};

struct PrimitiveInstance {
    Vertices vertices;
    Indices indices;
    int materialIndex;
};

layout(buffer_reference, scalar) buffer PrimitiveInstances {
    PrimitiveInstance primitiveInstances[];
};

layout(push_constant) uniform PushConstants {
    mat4 inverseView;
    mat4 inverseProj;
    PrimitiveInstances primitiveInstances;
    Materials materials;
} pc;

struct RayPayload {
    vec3 color;
    float t;
};

layout(set = 0, binding = 1) uniform sampler2D textures[];

void main() {
    PrimitiveInstance primitive = pc.primitiveInstances.primitiveInstances[gl_InstanceCustomIndexEXT];
    const uint index = gl_PrimitiveID * 3;

    Vertex v0 = primitive.vertices.vertices[primitive.indices.indices[index]];
    Vertex v1 = primitive.vertices.vertices[primitive.indices.indices[index + 1]];
    Vertex v2 = primitive.vertices.vertices[primitive.indices.indices[index + 2]];

    const vec3 barycentric = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    const vec2 uv = barycentric.x * v0.uv + barycentric.y * v1.uv + barycentric.z * v2.uv;

    const int textureIndex = pc.materials.materials[primitive.materialIndex].baseColorTexture;
    if (textureIndex == -1) {
        hitValue = pc.materials.materials[primitive.materialIndex].baseColorFactor.rgb;
        return;
    }

    const vec3 textureColor = texture(textures[pc.materials.materials[primitive.materialIndex].baseColorTexture], uv).rgb;
    hitValue = textureColor;
}
