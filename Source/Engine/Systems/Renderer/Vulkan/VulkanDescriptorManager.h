#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <unordered_map>

namespace Engine
{

	class VulkanDescriptorManager
	{

	public:

		VulkanDescriptorManager(VkDevice device, uint32_t maxSets = 1024, uint32_t maxBindlessTextures = 4096);
		~VulkanDescriptorManager();

		void CreateLayout();
		void CreatePool();

		void CreateUBODescriptorSet(VkBuffer buffer);

		// Bindless setup
		void CreateBindlessLayout();
		void CreateBindlessPool();
		void AllocateBindlessSet();
		void UpdateBindlessTexture(uint32_t index, VkImageView imageView, VkSampler sampler);
		void SetBindlessSampler(VkSampler sampler);

		VkDescriptorSetLayout GetLayout() const { return descriptorSetLayout; }
		VkDescriptorPool GetPool() const { return descriptorPool; }
		VkDescriptorSet GetDescriptorSetUBO() const { return uboDescriptorSet; }

		VkDescriptorSet GetBindlessSet() const { return bindlessDescriptorSet; }
		VkDescriptorSetLayout GetBindlessLayout() const { return bindlessSetLayout; }

		// Allocates and writes a descriptor set using the UBO and image sampler
		VkDescriptorSet AllocateSet(VkBuffer uniformBuffer, VkDeviceSize bufferSize, VkSampler sampler, VkImageView imageView);

		void Cleanup();

	private:

		VkDevice device;
		uint32_t maxSets;
		uint32_t maxBindlessTextures;

		// Standard (UBO + Texture) set
		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet uboDescriptorSet = VK_NULL_HANDLE;

		// Bindless global texture set
		VkDescriptorSetLayout bindlessSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool bindlessDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet bindlessDescriptorSet = VK_NULL_HANDLE;

	};

}
