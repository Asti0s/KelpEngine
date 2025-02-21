#include "DescriptorManager.hpp"

#include "Vulkan/VkDevice.hpp"
#include "Vulkan/VkUtils.hpp"
#include "vulkan/vulkan_core.h"

#define VK_NO_PROTOTYPES
#include "volk.h"

#include <array>
#include <cstdint>
#include <memory>

DescriptorManager::DescriptorManager(const std::shared_ptr<Vk::Device>& device) : m_device(device) {
    createDescriptorSetLayout();
    createDescriptorSet();
}

DescriptorManager::~DescriptorManager() {
    m_device->waitIdle();
    vkDestroyDescriptorSetLayout(m_device->getHandle(), m_descriptorSetLayout, nullptr);
}

uint32_t DescriptorManager::storeImage(VkImageView imageView) {
    const VkDescriptorImageInfo imageInfo{
        .imageView = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_descriptorSet,
        .dstBinding = STORAGE_IMAGE_BINDING,
        .dstArrayElement = m_storageImageCount,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfo
    };

    vkUpdateDescriptorSets(m_device->getHandle(), 1, &write, 0, nullptr);

    m_storageImageCount++;
    return m_storageImageCount - 1;
}

uint32_t DescriptorManager::storeSampledImage(VkImageView imageView, VkSampler sampler) {
    const VkDescriptorImageInfo imageInfo{
        .sampler = sampler,
        .imageView = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_descriptorSet,
        .dstBinding = COMBINED_IMAGE_SAMPLER_BINDING,
        .dstArrayElement = m_combinedImageSamplerCount,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };

    vkUpdateDescriptorSets(m_device->getHandle(), 1, &write, 0, nullptr);

    m_combinedImageSamplerCount++;
    return m_combinedImageSamplerCount - 1;
}

void DescriptorManager::storeAccelerationStructure(VkAccelerationStructureKHR accelerationStructure) {
    const VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &accelerationStructure
    };

    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = &accelerationStructureInfo,
        .dstSet = m_descriptorSet,
        .dstBinding = ACCELERATION_STRUCTURE_BINDING,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
    };

    vkUpdateDescriptorSets(m_device->getHandle(), 1, &write, 0, nullptr);
}

void DescriptorManager::createDescriptorSet() {
    const VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_device->getDescriptorPool(),
        .descriptorSetCount = 1,
        .pSetLayouts = &m_descriptorSetLayout
    };

    VK_CHECK(vkAllocateDescriptorSets(m_device->getHandle(), &allocInfo, &m_descriptorSet));
}

void DescriptorManager::createDescriptorSetLayout() {
    constexpr uint8_t descriptorCount = 3;
    constexpr std::array<VkDescriptorType, descriptorCount> types{
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
    };

    std::array<VkDescriptorBindingFlags, descriptorCount> flags{};
    std::array<VkDescriptorSetLayoutBinding, descriptorCount> bindings{};
    for (uint32_t i = 0; i < descriptorCount; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = types[i];
        bindings[i].descriptorCount = 1000;
        bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
        flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    }

    const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = descriptorCount,
        .pBindingFlags = flags.data()
    };

    const VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = descriptorCount,
        .pBindings = bindings.data(),
    };

    VK_CHECK(vkCreateDescriptorSetLayout(m_device->getHandle(), &layoutInfo, nullptr, &m_descriptorSetLayout));
}
