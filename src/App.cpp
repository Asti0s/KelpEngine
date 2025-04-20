#include "App.hpp"

#include "ShaderCompiler.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Swapchain.hpp"
#include "Vulkan/Utils.hpp"

#include "GLFW/glfw3.h"
#include "glm/ext/vector_int2.hpp"
#include "glm/matrix.hpp"
#include "glslang/Public/ShaderLang.h"
#include <vulkan/vulkan_core.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

App::App() {
    m_window->setResizeCallback([&](const glm::ivec2& size) {
        m_device->waitIdle();
        m_swapchain.resize(size);

        m_camera.setPerspective(90, static_cast<float>(size.x) / static_cast<float>(size.y), 0.1, 100);
        prepareOutputImage();
    });

    m_camera.setPerspective(90, static_cast<float>(m_window->getSize().x) / static_cast<float>(m_window->getSize().y), 0.1, 100);

    loadAssetsFromFile("../assets/Arcade/Arcade.gltf");
    prepareOutputImage();
    getRaytracingProperties();
    createRaytracingPipeline();
    createShaderBindingTable();
}

App::~App() {
    m_device->waitIdle();

    for (const auto& mesh : m_meshes)
        for (const auto& primitive : mesh->primitives)
            if (primitive.accelerationStructure.handle != VK_NULL_HANDLE)
                vkDestroyAccelerationStructureKHR(m_device->getHandle(), primitive.accelerationStructure.handle, VK_NULL_HANDLE);
    for (const auto& sampler : m_samplers)
        if (sampler != VK_NULL_HANDLE)
            vkDestroySampler(m_device->getHandle(), sampler, VK_NULL_HANDLE);

    if (m_topLevelAccelerationStructure != VK_NULL_HANDLE)
        vkDestroyAccelerationStructureKHR(m_device->getHandle(), m_topLevelAccelerationStructure, VK_NULL_HANDLE);

    if (m_hitShaderBindingTable != nullptr)
        m_hitShaderBindingTable->unmap();
    if (m_missShaderBindingTable != nullptr)
        m_missShaderBindingTable->unmap();
    if (m_raygenShaderBindingTable != nullptr)
        m_raygenShaderBindingTable->unmap();

    if (m_raytracingPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(m_device->getHandle(), m_raytracingPipeline, VK_NULL_HANDLE);

    if (m_pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device->getHandle(), m_pipelineLayout, VK_NULL_HANDLE);
}

void App::getRaytracingProperties() {
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
    };
    VkPhysicalDeviceProperties2 physicalDeviceProperties2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &rayTracingPipelineProperties,
    };
    vkGetPhysicalDeviceProperties2(m_device->getPhysicalDevice(), &physicalDeviceProperties2);
    m_raytracingProperties = rayTracingPipelineProperties;
}

void App::prepareOutputImage() {
    const Image::CreateInfo imageCreateInfo{
        .extent = {m_swapchain.getExtent().width, m_swapchain.getExtent().height, 1},
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
    };
    m_outputImage = std::make_unique<Image>(m_device, imageCreateInfo);
    m_descriptorManager.storeImage(m_outputImage->getImageView(), 0);

    VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Device::QueueType::Graphics); {
        m_outputImage->cmdTransitionLayout(commandBuffer,
            Image::Layout{ .layout = VK_IMAGE_LAYOUT_UNDEFINED, .accessMask = 0,                            .stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT },
            Image::Layout{ .layout = VK_IMAGE_LAYOUT_GENERAL,   .accessMask = VK_ACCESS_SHADER_WRITE_BIT,   .stageFlags = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR }
        );
    } m_device->endSingleTimeCommands(Device::QueueType::Graphics, commandBuffer);
}

static size_t align(size_t value, size_t alignment) {   // NOLINT
    return (value + alignment - 1) & ~(alignment - 1);
}

void App::createShaderBindingTable() {
    constexpr uint32_t groupCount = 3;
    const uint32_t handleSize = m_raytracingProperties.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = align(handleSize, m_raytracingProperties.shaderGroupHandleAlignment);
    const uint32_t sbtSize = groupCount * handleSizeAligned;

    constexpr VkBufferUsageFlags bufferUsage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    constexpr VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(m_device->getHandle(), m_raytracingPipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

    m_raygenShaderBindingTable = std::make_unique<Buffer>(m_device, sbtSize, bufferUsage, allocationFlags);
    m_missShaderBindingTable = std::make_unique<Buffer>(m_device, sbtSize, bufferUsage, allocationFlags);
    m_hitShaderBindingTable = std::make_unique<Buffer>(m_device, sbtSize, bufferUsage, allocationFlags);

    m_raygenShaderBindingTable->map(&m_mappedRaygenShaderBindingTable);
    m_missShaderBindingTable->map(&m_mappedMissShaderBindingTable);
    m_hitShaderBindingTable->map(&m_mappedHitShaderBindingTable);

    std::memcpy(m_mappedRaygenShaderBindingTable, shaderHandleStorage.data(), handleSize);
    std::memcpy(m_mappedMissShaderBindingTable, shaderHandleStorage.data() + handleSizeAligned, handleSize);
    std::memcpy(m_mappedHitShaderBindingTable, shaderHandleStorage.data() + static_cast<size_t>(handleSizeAligned * 2), handleSize);
}

void App::createRaytracingPipeline() {
    // Pipeline layout creation
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_ALL,
        .offset = 0,
        .size = sizeof(PushConstantData),
    };

    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_descriptorManager.getDescriptorSetLayout(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };

    VK_CHECK(vkCreatePipelineLayout(m_device->getHandle(), &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout));

    const std::array<VkPipelineShaderStageCreateInfo, 4> shaderStages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .module = ShaderCompiler::compileShader(m_device, "../shaders/raytracing.glsl", EShLangRayGen, "#define RAYGEN_SHADER\n"),
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .module = ShaderCompiler::compileShader(m_device, "../shaders/raytracing.glsl", EShLangMiss, "#define MISS_SHADER\n"),
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .module = ShaderCompiler::compileShader(m_device, "../shaders/raytracing.glsl", EShLangClosestHit, "#define CLOSEST_HIT_SHADER\n"),
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
            .module = ShaderCompiler::compileShader(m_device, "../shaders/raytracing.glsl", EShLangAnyHit, "#define ANY_HIT_SHADER\n"),
            .pName = "main",
        },
    };

    constexpr std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> shaderGroups = {
        VkRayTracingShaderGroupCreateInfoKHR{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = 0,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        },
        VkRayTracingShaderGroupCreateInfoKHR{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = 1,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        },
        VkRayTracingShaderGroupCreateInfoKHR{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
            .generalShader = VK_SHADER_UNUSED_KHR,
            .closestHitShader = 2,
            .anyHitShader = 3,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        },
    };

    const VkRayTracingPipelineCreateInfoKHR raytracingPipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .groupCount = static_cast<uint32_t>(shaderGroups.size()),
        .pGroups = shaderGroups.data(),
        .maxPipelineRayRecursionDepth = 1,
        .layout = m_pipelineLayout,
    };

    VK_CHECK(vkCreateRayTracingPipelinesKHR(m_device->getHandle(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &raytracingPipelineCreateInfo, nullptr, &m_raytracingPipeline));


    // Cleanup
    for (const auto& shaderStage : shaderStages)
        vkDestroyShaderModule(m_device->getHandle(), shaderStage.module, nullptr);
}

void App::handleEvents(float deltaTime) {
    m_window->pollEvents();

    if (m_window->isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
        m_window->setCursorVisible(false);
        m_camera.disableCursorCallback(false);
    } else {
        m_camera.disableCursorCallback(true);
        m_window->setCursorVisible(true);
        m_camera.resetMousePosition();
    }

    m_camera.update(deltaTime);
}

void App::traceRays(VkCommandBuffer commandBuffer) {
    const uint32_t handleSizeAligned = align(m_raytracingProperties.shaderGroupHandleSize, m_raytracingProperties.shaderGroupHandleAlignment);

    const VkStridedDeviceAddressRegionKHR raygenShaderSbtEntry{
        .deviceAddress = m_raygenShaderBindingTable->getDeviceAddress(),
        .stride = handleSizeAligned,
        .size = handleSizeAligned,
    };

    const VkStridedDeviceAddressRegionKHR missShaderSbtEntry{
        .deviceAddress = m_missShaderBindingTable->getDeviceAddress(),
        .stride = handleSizeAligned,
        .size = handleSizeAligned,
    };

    const VkStridedDeviceAddressRegionKHR hitShaderSbtEntry{
        .deviceAddress = m_hitShaderBindingTable->getDeviceAddress(),
        .stride = handleSizeAligned,
        .size = handleSizeAligned,
    };

    const VkStridedDeviceAddressRegionKHR callableShaderSbtEntry{};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_raytracingPipeline);
    vkCmdTraceRaysKHR(commandBuffer, &raygenShaderSbtEntry, &missShaderSbtEntry, &hitShaderSbtEntry, &callableShaderSbtEntry, m_swapchain.getExtent().width, m_swapchain.getExtent().height, 1);
}

void App::transferOutputImageToSwapchain(VkCommandBuffer commandBuffer) {
    m_outputImage->cmdTransitionLayout(commandBuffer,
        Image::Layout{ .layout = VK_IMAGE_LAYOUT_GENERAL,               .accessMask = VK_ACCESS_SHADER_WRITE_BIT,   .stageFlags = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR },
        Image::Layout{ .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,  .accessMask = VK_ACCESS_TRANSFER_READ_BIT,  .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT}
    );

    m_swapchain.getCurrentImage()->cmdTransitionLayout(commandBuffer,
        Image::Layout{ .layout = VK_IMAGE_LAYOUT_UNDEFINED,             .accessMask = 0,                            .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT },
        Image::Layout{ .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  .accessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT }
    );

    m_swapchain.getCurrentImage()->cmdCopyFromImage(commandBuffer, *m_outputImage);

    m_outputImage->cmdTransitionLayout(commandBuffer,
        Image::Layout{ .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,  .accessMask = VK_ACCESS_TRANSFER_READ_BIT,  .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT },
        Image::Layout{ .layout = VK_IMAGE_LAYOUT_GENERAL,               .accessMask = 0,                            .stageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }
    );

    m_swapchain.getCurrentImage()->cmdTransitionLayout(commandBuffer,
        Image::Layout{ .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  .accessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT },
        Image::Layout{ .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,       .accessMask = 0,                            .stageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT }
    );
}

void App::bindDescriptors(VkCommandBuffer commandBuffer) {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 1, &m_descriptorManager.getDescriptorSet(), 0, nullptr);

    const PushConstantData pushConstantData{
        .inverseView = glm::inverse(m_camera.getViewMatrix()),
        .inverseProjection = glm::inverse(m_camera.getProjectionMatrix()),
        .gpuPrimitiveInstancesBufferAddress = m_gpuPrimitiveInstancesBuffer->getDeviceAddress(),
        .gpuMaterialsBufferAddress = m_gpuMaterials->getDeviceAddress(),
    };
    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(PushConstantData), &pushConstantData);
}

void App::updateWindowTitle(float deltaTime) {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT memoryBudget{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };

    VkPhysicalDeviceMemoryProperties2 memoryProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = &memoryBudget,
    };

    vkGetPhysicalDeviceMemoryProperties2(m_device->getPhysicalDevice(), &memoryProperties);

    uint32_t vramBudget = 0;
    uint32_t vramUsage = 0;

    for (uint32_t i = 0; i < memoryProperties.memoryProperties.memoryHeapCount; i++) {
        if (static_cast<bool>(memoryProperties.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) {
            vramBudget += memoryBudget.heapBudget[i] / 1024 / 1024;
            vramUsage += memoryBudget.heapUsage[i] / 1024 / 1024;
        }
    }

    m_window->setTitle(std::string("Kelp Engine | " + std::to_string(vramUsage) + " MB / " + std::to_string(vramBudget) + " MB | " + std::to_string(static_cast<int>(1.0F / deltaTime)) + " FPS").c_str());
}

void App::run() {
    std::chrono::high_resolution_clock::time_point loopStart = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point loopEnd = std::chrono::high_resolution_clock::now();
    float deltaTime = 0;
    float accum = 0;
    uint32_t frameCount = 0;

    while (m_window->isOpen()) {
        loopStart = std::chrono::high_resolution_clock::now();

        updateWindowTitle(deltaTime);
        handleEvents(deltaTime);

        VkCommandBuffer commandBuffer = m_swapchain.beginFrame();
        {   // Render
            bindDescriptors(commandBuffer);
            traceRays(commandBuffer);
            transferOutputImageToSwapchain(commandBuffer);
        }
        m_swapchain.endFrame(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT);

        loopEnd = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<float>(loopEnd - loopStart).count();
        accum += deltaTime;
        frameCount++;
    }

    // avg frame time
    std::cout << "Average frame time: " << accum / static_cast<float>(frameCount) * 1000.0F << " ms" << std::endl;
    std::cout << "Average FPS: " << static_cast<float>(frameCount) / accum << std::endl;
}
