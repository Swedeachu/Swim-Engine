#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include "Buffers/VulkanBuffer.h"

namespace Engine
{

	class VulkanDescriptorManager
	{

	public:

		VulkanDescriptorManager
		(
			VkDevice device, 
			VkPhysicalDevice physicalDevice, 
			uint32_t maxSets = 1024, 
			uint32_t maxBindlessTextures = 4096, 
			uint64_t ssbosSize = 10240
		);

		~VulkanDescriptorManager();

		void CreateLayout();
		void CreatePool();

		// Create per-frame UBOs and descriptor sets
		void CreatePerFrameUBOs(VkPhysicalDevice physicalDevice, uint32_t frameCount);
		void UpdatePerFrameUBO(uint32_t frameIndex, const CameraUBO& ubo);

		VkDescriptorSet GetPerFrameDescriptorSet(uint32_t frameIndex) const;

		void UpdatePerFrameInstanceBuffer(uint32_t frameIndex, const void* data, size_t size);

		// Upload MeshDecoratorGpuInstanceData data to the per-frame buffer
		void UpdatePerFrameMeshDecoratorBuffer(uint32_t frameIndex, const void* data, size_t size);

		// Upload MsdfTextGpuInstanceData data to the per-frame buffer
		void UpdatePerFrameMsdfBuffer(uint32_t frameIndex, const void* data, size_t size);

		// Get the UIParam SSBO buffer for the current frame
		VulkanBuffer* GetMeshDecoratorBufferForFrame(uint32_t frameIndex) const;

		// Get the MsdfParam SSBO buffer for the current frame 
		VulkanBuffer* GetMsdfBufferForFrame(uint32_t frameIndex) const;

		// Adds SSBO (instance buffer) binding to per-frame descriptor sets
		void CreateInstanceBufferDescriptorSets(const std::vector<std::unique_ptr<VulkanBuffer>>& perFrameInstanceBuffers);

		// Bindless setup
		void CreateBindlessLayout();
		void CreateBindlessPool();
		void AllocateBindlessSet();
		void UpdateBindlessTexture(uint32_t index, VkImageView imageView, VkSampler sampler) const;
		void SetBindlessSampler(VkSampler sampler) const;

		VkDescriptorSetLayout GetLayout() const { return descriptorSetLayout; }
		VkDescriptorPool GetPool() const { return descriptorPool; }

		VkDescriptorSet GetBindlessSet() const { return bindlessDescriptorSet; }
		VkDescriptorSetLayout GetBindlessLayout() const { return bindlessSetLayout; }

		VulkanBuffer* GetPerFrameUBO(uint32_t frameIndex) const;

		VulkanBuffer* GetInstanceBufferForFrame(uint32_t frameIndex) const;

		void EnsurePerFrameInstanceCapacity(size_t bytes);
		void EnsurePerFrameMeshDecoratorCapacity(size_t bytes);
		void EnsurePerFrameMsdfCapacity(size_t bytes);

		void Cleanup();

	private:

		void EnsurePerFrameBufferCapacity(size_t bytes, std::vector<std::unique_ptr<VulkanBuffer>>& buffers);

		VkDevice device;
		VkPhysicalDevice physicalDevice;
		uint32_t maxSets;
		uint32_t maxBindlessTextures;
		uint64_t ssboSize;

		// Standard (UBO + Texture) set
		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

		// Bindless global texture set
		VkDescriptorSetLayout bindlessSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool bindlessDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet bindlessDescriptorSet = VK_NULL_HANDLE;

		// Per-frame uniform buffers and descriptor sets
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameUBOs;
		std::vector<VkDescriptorSet> perFrameDescriptorSets;

		// Per-frame instance SSBOs
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameInstanceBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameMeshDecoratorBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameMsdfBuffers;

	};

}
