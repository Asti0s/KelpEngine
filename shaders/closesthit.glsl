#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable

struct Payload {
    vec3 rayDirX;
    vec3 rayDirY;
    vec3 hitValue;
};

layout(location = 0) rayPayloadInEXT Payload payload;

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
    vec3 pos;
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

layout(set = 0, binding = 1) uniform sampler2D textures[];

vec3 computeBarycentrics(Vertex vertices[3], vec3 rayOrigin, vec3 rayDir) {
    const vec3 edge1 = vertices[1].pos - vertices[0].pos;
    const vec3 edge2 = vertices[2].pos - vertices[0].pos;
    const vec3 pvec = cross(rayDir, edge2);
    const float det = dot(edge1, pvec);
    const float invDet = 1.0 / det;
    const vec3 tvec = rayOrigin - vertices[0].pos;
    const float alpha = dot(tvec, pvec) * invDet;
    const vec3 qvec = cross(tvec, edge1);
    const float beta = dot(rayDir, qvec) * invDet;
    return vec3(1.0 - alpha - beta, alpha, beta);
}

void main() {
    PrimitiveInstance primitive = pc.primitiveInstances.primitiveInstances[gl_InstanceCustomIndexEXT];
    const uint index = gl_PrimitiveID * 3;

    Vertex v0 = primitive.vertices.vertices[primitive.indices.indices[index]];
    Vertex v1 = primitive.vertices.vertices[primitive.indices.indices[index + 1]];
    Vertex v2 = primitive.vertices.vertices[primitive.indices.indices[index + 2]];

    v0.pos = gl_ObjectToWorldEXT * vec4(v0.pos, 1.0);
    v1.pos = gl_ObjectToWorldEXT * vec4(v1.pos, 1.0);
    v2.pos = gl_ObjectToWorldEXT * vec4(v2.pos, 1.0);

    Vertex vertices[3] = {v0, v1, v2};

    vec3 barycentrics = computeBarycentrics(vertices, gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT);
    vec3 barycentricsX = computeBarycentrics(vertices, gl_WorldRayOriginEXT, payload.rayDirX);
    vec3 barycentricsY = computeBarycentrics(vertices, gl_WorldRayOriginEXT, payload.rayDirY);

    const vec2 texCoords = barycentrics.x * v0.uv + barycentrics.y * v1.uv + barycentrics.z * v2.uv;
    const vec2 texCoordsX = barycentricsX.x * v0.uv + barycentricsX.y * v1.uv + barycentricsX.z * v2.uv;
    const vec2 texCoordsY = barycentricsY.x * v0.uv + barycentricsY.y * v1.uv + barycentricsY.z * v2.uv;

    const vec2 texGradX = texCoordsY - texCoords;
    const vec2 texGradY = texCoordsX - texCoords;

    const int textureIndex = pc.materials.materials[primitive.materialIndex].baseColorTexture;
    if (textureIndex == -1) {
        payload.hitValue = pc.materials.materials[primitive.materialIndex].baseColorFactor.rgb;
        return;
    }

    const vec3 textureColor = textureGrad(textures[pc.materials.materials[primitive.materialIndex].baseColorTexture], texCoords, texGradX, texGradY).rgb;
    payload.hitValue = textureColor;
}
