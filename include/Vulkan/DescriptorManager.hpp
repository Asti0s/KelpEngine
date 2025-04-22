#pragma once

#include "Device.hpp"

#define VK_NO_PROTOTYPES
#include "volk.h"
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>

class DescriptorManager {
    public:
        DescriptorManager(const std::shared_ptr<Device>& device);
        ~DescriptorManager();

        DescriptorManager(const DescriptorManager&) = delete;
        DescriptorManager& operator=(const DescriptorManager&) = delete;

        DescriptorManager(DescriptorManager&&) noexcept = delete;
        DescriptorManager& operator=(DescriptorManager&&) = delete;


        /**
         * @brief Store an image view in the array of storage images of the descriptor set and return the dst array index.
         * The index should be passed to the shader to access the image through the bindless texture extension at binding STORAGE_IMAGE_BINDING
         *
         * @param imageView image view to store
         * @return uint32_t index of the stored image
         */
        uint32_t storeImage(VkImageView imageView);

        /**
         * @brief Store an image view in the array of storage images of the descriptor set at index index.
         *
         * @param imageView image view to store
         * @param index index of the image in the array
         */
        void storeImage(VkImageView imageView, uint32_t index);

        /**
         * @brief Store an image view and a sampler in the array of combined image samplers of the descriptor set and return the dst array index.
         * The index should be passed to the shader to access the image through the bindless texture extension at binding COMBINED_IMAGE_SAMPLER_BINDING.
         *
         * @param imageView image view to store
         * @param sampler sampler to store
         * @return uint32_t index of the stored image
         */
        uint32_t storeSampledImage(VkImageView imageView, VkSampler sampler);

        /**
         * @brief Store an acceleration structure in the descriptor set at binding ACCELERATION_STRUCTURE_BINDING
         *
         * @param accelerationStructure acceleration structure to store
         */
        void storeAccelerationStructure(VkAccelerationStructureKHR accelerationStructure);


        /* Getters */
        [[nodiscard]] const VkDescriptorSetLayout &getDescriptorSetLayout() const { return m_descriptorSetLayout; }
        [[nodiscard]] const VkDescriptorSet &getDescriptorSet() const { return m_descriptorSet; }


    private:
        void createDescriptorSetLayout();
        void createDescriptorSet();


    private:
        std::shared_ptr<Device> m_device;

        VkDescriptorSetLayout m_descriptorSetLayout{};
        VkDescriptorSet m_descriptorSet{};

        uint32_t m_storageImageCount = 0;
        uint32_t m_combinedImageSamplerCount = 0;
};
