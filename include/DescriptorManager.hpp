#pragma once

#include "Vulkan/VkDevice.hpp"

#define VK_NO_PROTOTYPES
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>

class DescriptorManager {
    public:
        const uint32_t STORAGE_IMAGE_BINDING = 0;
        const uint32_t COMBINED_IMAGE_SAMPLER_BINDING = 1;
        const uint32_t ACCELERATION_STRUCTURE_BINDING = 2;


    public:
        DescriptorManager(const std::shared_ptr<Vk::Device>& device);
        ~DescriptorManager();

        DescriptorManager(const DescriptorManager&) = delete;
        DescriptorManager& operator=(const DescriptorManager&) = delete;

        DescriptorManager(DescriptorManager&&) noexcept = delete;
        DescriptorManager& operator=(DescriptorManager&&) = delete;


        uint32_t storeImage(VkImageView imageView);
        uint32_t storeSampledImage(VkImageView imageView, VkSampler sampler);
        void storeAccelerationStructure(VkAccelerationStructureKHR accelerationStructure);


        /* Getters */
        [[nodiscard]] const VkDescriptorSetLayout &getDescriptorSetLayout() const { return m_descriptorSetLayout; }
        [[nodiscard]] const VkDescriptorSet &getDescriptorSet() const { return m_descriptorSet; }


    private:
        void createDescriptorSetLayout();
        void createDescriptorSet();


    private:
        std::shared_ptr<Vk::Device> m_device;

        VkDescriptorSetLayout m_descriptorSetLayout{};
        VkDescriptorSet m_descriptorSet{};

        uint32_t m_storageImageCount = 0;
        uint32_t m_combinedImageSamplerCount = 0;
};
