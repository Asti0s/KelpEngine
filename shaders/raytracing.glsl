#version 460

#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#include "shared.hpp"


layout(set = 0, binding = STORAGE_IMAGE_BINDING, rgba8) uniform image2D outputImage;
layout(set = 0, binding = COMBINED_IMAGE_SAMPLER_BINDING) uniform sampler2D textures[];
layout(set = 0, binding = ACCELERATION_STRUCTURE_BINDING) uniform accelerationStructureEXT topLevelAS;

layout(push_constant) uniform _ {
    PushConstant data;
} pc;

struct Payload {
    vec3 rayDirX;
    vec3 rayDirY;
    vec3 hitValue;
};

struct TexCoords {
    vec2 texCoords;
    vec2 texGradX;
    vec2 texGradY;
};


vec3 getRayDir(uvec2 pixelPos) {
    vec2 d = (vec2(pixelPos) + vec2(0.5)) / gl_LaunchSizeEXT.xy;
    d.y = 1.0 - d.y;
    vec4 target = pc.data.inverseProjection * vec4(d * 2.0 - 1.0, 1, 1);
    vec4 direction = pc.data.inverseView * vec4(normalize(target.xyz), 0);
    return direction.xyz;
}

vec3 computeBarycentrics(Vertex vertices[3], vec3 rayOrigin, vec3 rayDir) {
    const vec3 edge1 = vertices[1].position - vertices[0].position;
    const vec3 edge2 = vertices[2].position - vertices[0].position;
    const vec3 pvec = cross(rayDir, edge2);
    const float det = dot(edge1, pvec);
    const float invDet = 1.0 / det;
    const vec3 tvec = rayOrigin - vertices[0].position;
    const float alpha = dot(tvec, pvec) * invDet;
    const vec3 qvec = cross(tvec, edge1);
    const float beta = dot(rayDir, qvec) * invDet;
    return vec3(1.0 - alpha - beta, alpha, beta);
}


#ifdef RAYGEN_SHADER
    layout(location = 0) rayPayloadEXT Payload payload;

    void main() {
        vec4 origin = pc.data.inverseView * vec4(0, 0, 0, 1);
        vec3 direction = getRayDir(gl_LaunchIDEXT.xy);
        vec3 rayDirX = getRayDir(gl_LaunchIDEXT.xy + uvec2(1, 0));
        vec3 rayDirY = getRayDir(gl_LaunchIDEXT.xy + uvec2(0, 1));

        float tmin = 0.001;
        float tmax = 10000.0;

        payload = Payload(rayDirX, rayDirY, vec3(0.0));

        traceRayEXT(topLevelAS, gl_RayFlagsNoneEXT, 0xff, 0, 0, 0, origin.xyz, tmin, direction.xyz, tmax, 0);

        imageStore(outputImage, ivec2(gl_LaunchIDEXT.xy), vec4(payload.hitValue, 0.0));
    }
#endif // RAYGEN_SHADER


#ifdef ANY_HIT_SHADER
    layout(location = 0) rayPayloadInEXT Payload payload;

    void main() {
        MeshInstance mesh = pc.data.meshInstanceBuffer.meshInstances[gl_InstanceCustomIndexEXT];
        const uint index = gl_PrimitiveID * 3;

        Vertex v0 = mesh.vertexBuffer.vertices[mesh.indexBuffer.indices[index]];
        Vertex v1 = mesh.vertexBuffer.vertices[mesh.indexBuffer.indices[index + 1]];
        Vertex v2 = mesh.vertexBuffer.vertices[mesh.indexBuffer.indices[index + 2]];

        v0.position = gl_ObjectToWorldEXT * vec4(v0.position, 1.0);
        v1.position = gl_ObjectToWorldEXT * vec4(v1.position, 1.0);
        v2.position = gl_ObjectToWorldEXT * vec4(v2.position, 1.0);

        Vertex vertices[3] = {v0, v1, v2};

        vec3 barycentrics = computeBarycentrics(vertices, gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT);
        vec3 barycentricsX = computeBarycentrics(vertices, gl_WorldRayOriginEXT, payload.rayDirX);
        vec3 barycentricsY = computeBarycentrics(vertices, gl_WorldRayOriginEXT, payload.rayDirY);

        const vec2 texCoords = barycentrics.x * v0.uv + barycentrics.y * v1.uv + barycentrics.z * v2.uv;
        const vec2 texCoordsX = barycentricsX.x * v0.uv + barycentricsX.y * v1.uv + barycentricsX.z * v2.uv;
        const vec2 texCoordsY = barycentricsY.x * v0.uv + barycentricsY.y * v1.uv + barycentricsY.z * v2.uv;

        const vec2 texGradX = texCoordsY - texCoords;
        const vec2 texGradY = texCoordsX - texCoords;

        const Material material = pc.data.materialBuffer.materials[mesh.materialIndex];
        if (material.alphaTexture == -1)
            return;

        const float alpha = textureGrad(textures[material.alphaTexture], texCoords, texGradX, texGradY).r;
        if (alpha < material.alphaCutoff)
            ignoreIntersectionEXT;
    }
#endif // ANY_HIT_SHADER


#ifdef CLOSEST_HIT_SHADER
    layout(location = 0) rayPayloadInEXT Payload payload;

    void main() {
        MeshInstance mesh = pc.data.meshInstanceBuffer.meshInstances[gl_InstanceCustomIndexEXT];
        const uint index = gl_PrimitiveID * 3;

        Vertex v0 = mesh.vertexBuffer.vertices[mesh.indexBuffer.indices[index]];
        Vertex v1 = mesh.vertexBuffer.vertices[mesh.indexBuffer.indices[index + 1]];
        Vertex v2 = mesh.vertexBuffer.vertices[mesh.indexBuffer.indices[index + 2]];

        v0.position = gl_ObjectToWorldEXT * vec4(v0.position, 1.0);
        v1.position = gl_ObjectToWorldEXT * vec4(v1.position, 1.0);
        v2.position = gl_ObjectToWorldEXT * vec4(v2.position, 1.0);

        Vertex vertices[3] = {v0, v1, v2};

        vec3 barycentrics = computeBarycentrics(vertices, gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT);
        vec3 barycentricsX = computeBarycentrics(vertices, gl_WorldRayOriginEXT, payload.rayDirX);
        vec3 barycentricsY = computeBarycentrics(vertices, gl_WorldRayOriginEXT, payload.rayDirY);

        const vec2 texCoords = barycentrics.x * v0.uv + barycentrics.y * v1.uv + barycentrics.z * v2.uv;
        const vec2 texCoordsX = barycentricsX.x * v0.uv + barycentricsX.y * v1.uv + barycentricsX.z * v2.uv;
        const vec2 texCoordsY = barycentricsY.x * v0.uv + barycentricsY.y * v1.uv + barycentricsY.z * v2.uv;

        const vec2 texGradX = texCoordsY - texCoords;
        const vec2 texGradY = texCoordsX - texCoords;

        const int textureIndex = pc.data.materialBuffer.materials[mesh.materialIndex].baseColorTexture;
        if (textureIndex == -1) {
            payload.hitValue = pc.data.materialBuffer.materials[mesh.materialIndex].baseColorFactor.rgb;
            return;
        }

        payload.hitValue = textureGrad(textures[textureIndex], texCoords, texGradX, texGradY).rgb;
    }
#endif // CLOSEST_HIT_SHADER


#ifdef MISS_SHADER
    layout(location = 0) rayPayloadInEXT Payload payload;

    void main() {
        payload.hitValue = vec3(0.0, 0.0, 0.0);
    }
#endif // MISS_SHADER
