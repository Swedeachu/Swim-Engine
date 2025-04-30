#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <stdexcept>

namespace Engine
{

	class VulkanDescriptorManager
	{

	public:

		VulkanDescriptorManager(VkDevice device, uint32_t maxSets = 1024);
		~VulkanDescriptorManager();

		void CreateLayout();
		void CreatePool();

		VkDescriptorSetLayout GetLayout() const { return descriptorSetLayout; }
		VkDescriptorPool GetPool() const { return descriptorPool; }

		// Allocates and writes a descriptor set using the UBO and image sampler
		VkDescriptorSet AllocateSet(VkBuffer uniformBuffer, VkDeviceSize bufferSize, VkSampler sampler, VkImageView imageView);

		void Cleanup();

	private:

		VkDevice device;
		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		uint32_t maxSets;

	};

}
