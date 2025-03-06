#version 460
#extension GL_EXT_ray_tracing : enable

layout(binding = 2, set = 0) uniform accelerationStructureEXT topLevelAS[];
layout(binding = 0, set = 0, rgba8) uniform image2D image[];

layout(push_constant) uniform PushConstants {
    mat4 inverseView;
    mat4 inverseProj;
} cam;

struct Payload {
    vec3 rayDirX;
    vec3 rayDirY;
    vec3 hitValue;
};

layout(location = 0) rayPayloadEXT Payload payload;

vec3 getRayDir(uvec2 pixelPos) {
    vec2 d = (vec2(pixelPos) + vec2(0.5)) / gl_LaunchSizeEXT.xy;
    d.y = 1.0 - d.y;
    vec4 target = cam.inverseProj * vec4(d * 2.0 - 1.0, 1, 1);
    vec4 direction = cam.inverseView * vec4(normalize(target.xyz), 0);
    return direction.xyz;
}

void main() {
    vec4 origin = cam.inverseView * vec4(0, 0, 0, 1);
    vec3 direction = getRayDir(gl_LaunchIDEXT.xy);
    vec3 rayDirX = getRayDir(gl_LaunchIDEXT.xy + uvec2(1, 0));
    vec3 rayDirY = getRayDir(gl_LaunchIDEXT.xy + uvec2(0, 1));

    float tmin = 0.001;
    float tmax = 10000.0;

    payload = Payload(rayDirX, rayDirY, vec3(0.0));

    traceRayEXT(topLevelAS[0], gl_RayFlagsNoneEXT, 0xff, 0, 0, 0, origin.xyz, tmin, direction.xyz, tmax, 0);

    imageStore(image[0], ivec2(gl_LaunchIDEXT.xy), vec4(payload.hitValue, 0.0));
}
