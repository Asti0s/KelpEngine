#version 460
#extension GL_EXT_ray_tracing : enable

layout(binding = 2, set = 0) uniform accelerationStructureEXT topLevelAS[];
layout(binding = 0, set = 0, rgba8) uniform image2D image[];

layout(push_constant) uniform PushConstants {
    mat4 inverseView;
    mat4 inverseProj;
} cam;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main() {
    const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
    const vec2 inUV = pixelCenter/vec2(gl_LaunchSizeEXT.xy);
    vec2 d = vec2(inUV.x, 1.0 - inUV.y) * 2.0 - 1.0;

    vec4 origin = cam.inverseView * vec4(0, 0, 0, 1);
    vec4 target = cam.inverseProj * vec4(d.x, d.y, 1, 1);
    vec4 direction = cam.inverseView * vec4(normalize(target.xyz), 0);

    float tmin = 0.001;
    float tmax = 10000.0;

    hitValue = vec3(0.0);

    traceRayEXT(topLevelAS[0], gl_RayFlagsNoneEXT, 0xff, 0, 0, 0, origin.xyz, tmin, direction.xyz, tmax, 0);

    imageStore(image[0], ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 0.0));
}
