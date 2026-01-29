#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>

#include "Buffers/VulkanBuffer.h"

namespace Engine
{

	// Forward declares 
	struct CameraUBO;

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

		// Get the MeshDecorator SSBO buffer for the current frame
		VulkanBuffer* GetMeshDecoratorBufferForFrame(uint32_t frameIndex) const;

		// Get the Msdf SSBO buffer for the current frame
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

		// NOTE: These functions will recreate the per-frame buffers if needed AND update the per-frame descriptor sets.
		void EnsurePerFrameInstanceCapacity(size_t bytes);
		void EnsurePerFrameMeshDecoratorCapacity(size_t bytes);
		void EnsurePerFrameMsdfCapacity(size_t bytes);

		// Per-frame cull output buffers
		void CreatePerFrameCullBuffers(uint32_t frameCount, uint32_t maxCommands);

		// Convenience (VulkanIndexDraw wants raw VkBuffer handles)
		VkBuffer GetPerFrameCullCommandBuffer(uint32_t frameIndex) const;
		VkBuffer GetPerFrameCullCountBuffer(uint32_t frameIndex) const;

		// If still want direct access to the wrapper buffers
		VulkanBuffer* GetCullIndirectBufferForFrame(uint32_t frameIndex) const;
		VulkanBuffer* GetCullCountBufferForFrame(uint32_t frameIndex) const;

		void Cleanup();

	private:

		void EnsurePerFrameBufferCapacity
		(
			size_t bytes,
			std::vector<std::unique_ptr<VulkanBuffer>>& buffers,
			uint32_t dstBinding
		);

		void RewritePerFrameStorageBinding(uint32_t frameIndex, uint32_t dstBinding, VulkanBuffer& buffer);

		VkDevice device;
		VkPhysicalDevice physicalDevice;
		uint32_t maxSets;
		uint32_t maxBindlessTextures;
		uint64_t ssboSize;

		// Standard (UBO + SSBO) set
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

		// Per-frame cull output buffers
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullIndirectBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullCountBuffers;

	};

}
