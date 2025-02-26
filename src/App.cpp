#include "App.hpp"

#include "Vulkan/VkDevice.hpp"
#include "Vulkan/VkImage.hpp"
#include "Vulkan/VkSwapchain.hpp"
#include "Vulkan/VkUtils.hpp"

#include "GLFW/glfw3.h"
#include "glm/ext/vector_int2.hpp"
#include "glm/matrix.hpp"
#include "glslang/MachineIndependent/Versions.h"
#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include "glslang/SPIRV/GlslangToSpv.h"
#include <vulkan/vulkan_core.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

App::App() {
    const auto prepareOutputImage = [&](const std::unique_ptr<Vk::Image>& outputImage) {
        VkCommandBuffer commandBuffer = m_device->beginSingleTimeCommands(Vk::Device::QueueType::Graphics); {
            outputImage->cmdImagebarrier(commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                0,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL
            );
        } m_device->endSingleTimeCommands(Vk::Device::QueueType::Graphics, commandBuffer);
    };

    m_window->setResizeCallback([&](const glm::ivec2& size) {
        m_device->waitIdle();
        m_swapchain.resize(size);

        m_camera.setPerspective(90, static_cast<float>(size.x) / static_cast<float>(size.y), 0.01, 100);

        m_outputImage = std::make_unique<Vk::Image>(m_device, VkExtent3D{m_swapchain.getExtent().width, m_swapchain.getExtent().height, 1}, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_FORMAT_R8G8B8A8_UNORM);
        m_outputImageBindlessId = m_descriptorManager.storeImage(m_outputImage->getImageView());
        prepareOutputImage(m_outputImage);
    });

    m_camera.setPerspective(90, static_cast<float>(m_window->getSize().x) / static_cast<float>(m_window->getSize().y), 0.01, 100);

    m_outputImage = std::make_unique<Vk::Image>(m_device, VkExtent3D{m_swapchain.getExtent().width, m_swapchain.getExtent().height, 1}, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_FORMAT_R8G8B8A8_UNORM);
    m_outputImageBindlessId = m_descriptorManager.storeImage(m_outputImage->getImageView());
    prepareOutputImage(m_outputImage);

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
    };
    VkPhysicalDeviceProperties2 physicalDeviceProperties2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &rayTracingPipelineProperties,
    };
    vkGetPhysicalDeviceProperties2(m_device->getPhysicalDevice(), &physicalDeviceProperties2);
    m_raytracingProperties = rayTracingPipelineProperties;

    createRaytracingPipeline();
    createShaderBindingTable();

    loadAssetsFromFile("../assets/sponza.glb");
}

App::~App() {
    m_device->waitIdle();

    for (const auto& mesh : m_meshes)
        for (const auto& primitive : mesh.primitives)
            vkDestroyAccelerationStructureKHR(m_device->getHandle(), primitive.accelerationStructure.handle, VK_NULL_HANDLE);
    for (const auto& sampler : m_samplers)
        vkDestroySampler(m_device->getHandle(), sampler, VK_NULL_HANDLE);

    vkDestroyAccelerationStructureKHR(m_device->getHandle(), m_topLevelAccelerationStructure, VK_NULL_HANDLE);

    m_hitShaderBindingTable->unmap();
    m_missShaderBindingTable->unmap();
    m_raygenShaderBindingTable->unmap();

    vkDestroyPipeline(m_device->getHandle(), m_raytracingPipeline, VK_NULL_HANDLE);
    vkDestroyPipelineLayout(m_device->getHandle(), m_pipelineLayout, VK_NULL_HANDLE);
}

static size_t align(size_t value, size_t alignment) {   // NOLINT
    return (value + alignment - 1) & ~(alignment - 1);
}

void App::createShaderBindingTable() {
    const uint32_t handleSize = m_raytracingProperties.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = align(handleSize, m_raytracingProperties.shaderGroupHandleAlignment);
    const uint32_t groupCount = 3;
    const uint32_t sbtSize = groupCount * handleSizeAligned;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(m_device->getHandle(), m_raytracingPipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

    const VkBufferUsageFlags bufferUsage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    m_raygenShaderBindingTable = std::make_unique<Vk::Buffer>(m_device, sbtSize, bufferUsage, allocationFlags);
    m_missShaderBindingTable = std::make_unique<Vk::Buffer>(m_device, sbtSize, bufferUsage, allocationFlags);
    m_hitShaderBindingTable = std::make_unique<Vk::Buffer>(m_device, sbtSize, bufferUsage, allocationFlags);

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

    const std::array<VkPipelineShaderStageCreateInfo, 3> shaderStages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .module = compileShader("../shaders/raygen.glsl", EShLangRayGen),
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .module = compileShader("../shaders/miss.glsl", EShLangMiss),
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .module = compileShader("../shaders/closesthit.glsl", EShLangClosestHit),
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
            .anyHitShader = VK_SHADER_UNUSED_KHR,
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

VkShaderModule App::compileShader(const std::string& path, EShLanguage stage) {
    glslang::InitializeProcess();

    // Parameters
    constexpr int glslVersion = 460;
    constexpr bool forwardCompatible = false;
    constexpr EProfile profile = ECoreProfile;
    constexpr EShMessages messageFlags = EShMsgDefault;
    glslang::TShader::ForbidIncluder includer;


    // Load shader source
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Failed to compile shader \"" + path + "\": file not found");

    const std::string shaderSourceStr = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    const std::array<const char *, 1> shaderSource = { shaderSourceStr.c_str() };


    // Shader config
    glslang::TShader shader(stage);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4);
    shader.setEnvTarget(glslang::EshTargetSpv, glslang::EShTargetSpv_1_6);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, glslVersion);
    shader.setEntryPoint("main");
    shader.setStrings(shaderSource.data(), 1);


    // Preprocess
    std::string preprocessedShaderStr;
    if (!shader.preprocess(GetDefaultResources(), glslVersion, profile, false, forwardCompatible, messageFlags, &preprocessedShaderStr, includer))
        throw std::runtime_error("Failed to preprocess shader \"" + path + "\": " + shader.getInfoLog());
    const std::array<const char *, 1> preprocessedShader = { preprocessedShaderStr.c_str() };


    // Parse
    shader.setStrings(preprocessedShader.data(), 1);
    if (!shader.parse(GetDefaultResources(), glslVersion, profile, false, forwardCompatible, messageFlags, includer))
        throw std::runtime_error("Failed to parse shader \"" + path + "\": " + shader.getInfoLog());


    // Link
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messageFlags))
        throw std::runtime_error("Failed to link shader \"" + path + "\": " + program.getInfoLog());


    // Compile
    std::vector<uint32_t> spirvCode;
    glslang::SpvOptions options{ .validate = true };
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirvCode, &options);

    glslang::FinalizeProcess();


    // Shader module creation
    const VkShaderModuleCreateInfo shaderModuleCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirvCode.size() * sizeof(uint32_t),
        .pCode = spirvCode.data(),
    };

    VkShaderModule shaderModule{};
    VK_CHECK(vkCreateShaderModule(m_device->getHandle(), &shaderModuleCreateInfo, nullptr, &shaderModule));

    return shaderModule;
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

void App::run() {
    std::chrono::high_resolution_clock::time_point loopStart = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point loopEnd = std::chrono::high_resolution_clock::now();
    float deltaTime = 0;

    while (m_window->isOpen()) {
        loopStart = std::chrono::high_resolution_clock::now();

        handleEvents(deltaTime);

        m_swapchain.beginFrame();
        VkCommandBuffer commandBuffer = m_swapchain.getCurrentCommandBuffer();





        // Bind things
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_raytracingPipeline);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 1, &m_descriptorManager.getDescriptorSet(), 0, nullptr);

        const PushConstantData pushConstantData{
            .inverseView = glm::inverse(m_camera.getViewMatrix()),
            .inverseProjection = glm::inverse(m_camera.getProjectionMatrix()),
            .gpuPrimitiveInstancesBufferAddress = m_gpuPrimitiveInstancesBuffer->getDeviceAddress(),
            .gpuMaterialsBufferAddress = m_gpuMaterials->getDeviceAddress(),
        };
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(PushConstantData), &pushConstantData);




        // Trace rays
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

        vkCmdTraceRaysKHR(commandBuffer, &raygenShaderSbtEntry, &missShaderSbtEntry, &hitShaderSbtEntry, &callableShaderSbtEntry, m_swapchain.getExtent().width, m_swapchain.getExtent().height, 1);




        // Transition ouput image to transfer source
        m_outputImage->cmdImagebarrier(commandBuffer,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );


        // Transition swapchain image to transfer destination
        m_swapchain.acquireImage();

        VkImageMemoryBarrier swapchainImageBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_swapchain.getCurrentImage(),
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapchainImageBarrier);


        // Copy output image to swapchain image
        const VkImageCopy imageCopy{
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffset = { 0, 0, 0 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffset = { 0, 0, 0 },
            .extent = { m_swapchain.getExtent().width, m_swapchain.getExtent().height, 1 },
        };

        vkCmdCopyImage(commandBuffer, m_outputImage->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_swapchain.getCurrentImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);


        // Transition swapchain image to present
        swapchainImageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swapchainImageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        swapchainImageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapchainImageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapchainImageBarrier);


        // Transition output image to general
        m_outputImage->cmdImagebarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL
        );



        m_swapchain.endFrame();

        loopEnd = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<float>(loopEnd - loopStart).count();

        m_window->setTitle(std::string("Kelp Engine | " + std::to_string(static_cast<int>(1.0F / deltaTime)) + " FPS").c_str());
    }
}
