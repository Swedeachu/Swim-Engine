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
		void CreateTrueBatchCullLayout();
		void CreatePool();

		// Create per-frame UBOs and descriptor sets
		void CreatePerFrameUBOs(VkPhysicalDevice physicalDevice, uint32_t frameCount);
		void UpdatePerFrameUBO(uint32_t frameIndex, const CameraUBO& ubo);

		// Standard per-frame set (set 0): binding 1 points at the CPU/None instance buffer
		VkDescriptorSet GetPerFrameDescriptorSet(uint32_t frameIndex) const;

		// Culled world per-frame set (set 0): binding 1 points at the GPU compacted visible instance buffer
		VkDescriptorSet GetPerFrameCulledWorldDescriptorSet(uint32_t frameIndex) const;

		void UpdatePerFrameInstanceBuffer(uint32_t frameIndex, const void* data, size_t size);

		// Upload MeshDecoratorGpuInstanceData data to the per-frame buffer
		void UpdatePerFrameMeshDecoratorBuffer(uint32_t frameIndex, const void* data, size_t size);

		// Upload MsdfTextGpuInstanceData data to the per-frame buffer
		void UpdatePerFrameMsdfBuffer(uint32_t frameIndex, const void* data, size_t size);

		// Get the MeshDecorator SSBO buffer for the current frame
		VulkanBuffer* GetMeshDecoratorBufferForFrame(uint32_t frameIndex) const;

		// Get the Msdf SSBO buffer for the current frame
		VulkanBuffer* GetMsdfBufferForFrame(uint32_t frameIndex) const;

		// Adds SSBO (instance buffer) binding to per-frame descriptor sets (standard set only)
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

		// True-batched cull set layout (compute set0 for the 6 entry points)
		VkDescriptorSetLayout GetTrueBatchCullLayout() const { return trueBatchCullSetLayout; }

		VulkanBuffer* GetPerFrameUBO(uint32_t frameIndex) const;

		VulkanBuffer* GetInstanceBufferForFrame(uint32_t frameIndex) const;

		// NOTE: These functions will recreate the per-frame buffers if needed AND update the per-frame descriptor sets.
		void EnsurePerFrameInstanceCapacity(size_t bytes);
		void EnsurePerFrameMeshDecoratorCapacity(size_t bytes);
		void EnsurePerFrameMsdfCapacity(size_t bytes);

		// Per-frame cull output buffers (legacy path)
		void CreatePerFrameCullBuffers(uint32_t frameCount, uint32_t maxCommands);

		// Convenience (VulkanIndexDraw wants raw VkBuffer handles)
		VkBuffer GetPerFrameCullCommandBuffer(uint32_t frameIndex) const;
		VkBuffer GetPerFrameCullCountBuffer(uint32_t frameIndex) const;

		// If still want direct access to the wrapper buffers
		VulkanBuffer* GetCullIndirectBufferForFrame(uint32_t frameIndex) const;
		VulkanBuffer* GetCullCountBufferForFrame(uint32_t frameIndex) const;

		// MeshInfo buffer is persistent (not per-frame). Keep mesh IDs dense.
		void UpdateMeshInfo(uint32_t meshID, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset);
		void UpdateMeshInfoBuffer(const void* data, size_t size, uint32_t meshCount);
		uint32_t GetMeshInfoCount() const;

		// Ensure per-frame true-batch buffers exist + are big enough, and the true-batch descriptor sets are allocated/updated.
		void EnsurePerFrameTrueBatchCullCapacity(uint32_t frameIndex, uint32_t instanceCount, uint32_t meshCount, uint32_t meshGroupCount);

		// True-batch descriptor set (contains all compute bindings)
		VkDescriptorSet GetPerFrameTrueBatchCullDescriptorSet(uint32_t frameIndex) const;

		// True-batch raw buffers (VulkanIndexDraw wants VkBuffer handles)
		VkBuffer GetPerFrameCullMeshCountsBuffer(uint32_t frameIndex) const;
		VkBuffer GetPerFrameCullMeshOffsetsBuffer(uint32_t frameIndex) const;
		VkBuffer GetPerFrameCullMeshWriteCursorBuffer(uint32_t frameIndex) const;
		VkBuffer GetPerFrameCullGroupSumsBuffer(uint32_t frameIndex) const;
		VkBuffer GetPerFrameCullGroupOffsetsBuffer(uint32_t frameIndex) const;
		VkBuffer GetPerFrameCullVisibleInstanceBuffer(uint32_t frameIndex) const;

		void RewriteTrueBatchInstanceBinding(uint32_t frameIndex, VulkanBuffer& buffer);

		void Cleanup();

	private:

		void EnsurePerFrameBufferCapacity
		(
			size_t bytes,
			std::vector<std::unique_ptr<VulkanBuffer>>& buffers,
			uint32_t dstBinding
		);

		void EnsureMeshInfoCapacity(uint32_t requiredMeshCount);
		void WriteMeshInfoDescriptorSets();

		void RewritePerFrameStorageBinding(VkDescriptorSet dstSet, uint32_t dstBinding, VulkanBuffer& buffer);
		void RewritePerFrameStorageBinding(uint32_t frameIndex, uint32_t dstBinding, VulkanBuffer& buffer);

		// When cull buffers are (re)created, update bindings 4/5 in BOTH standard and culled-world per-frame sets.
		void RewritePerFrameCullBindings(uint32_t frameIndex);

		// Ensure the culled-world set0 binding 1 points at visibleInstanceBuffer when available.
		void RewriteCulledWorldInstanceBinding(uint32_t frameIndex);

		// True-batch helpers
		void AllocateTrueBatchDescriptorSetsIfNeeded();
		void UpdateTrueBatchDescriptorSetForFrame(uint32_t frameIndex);
		void EnsureDeviceLocalBuffer(std::unique_ptr<VulkanBuffer>& buf, VkDeviceSize bytes, VkBufferUsageFlags usage);

		VkDevice device;
		VkPhysicalDevice physicalDevice;
		uint32_t maxSets;
		uint32_t maxBindlessTextures;
		uint64_t ssboSize;

		uint32_t frameCount = 0;

		// Standard (UBO + SSBO) set
		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

		// True-batch compute set
		VkDescriptorSetLayout trueBatchCullSetLayout = VK_NULL_HANDLE;

		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

		// Bindless global texture set
		VkDescriptorSetLayout bindlessSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool bindlessDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet bindlessDescriptorSet = VK_NULL_HANDLE;

		// Per-frame uniform buffers and descriptor sets (standard path)
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameUBOs;
		std::vector<VkDescriptorSet> perFrameDescriptorSets;

		// Per-frame descriptor sets for GPU-culled world draw (same layout, binding 1 points to visible instances)
		std::vector<VkDescriptorSet> perFrameCulledWorldDescriptorSets;

		// Per-frame instance SSBOs
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameInstanceBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameMeshDecoratorBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameMsdfBuffers;

		// Per-frame cull output buffers (legacy, also reused by true-batch as outCommands/outDrawCount)
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullIndirectBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullCountBuffers;

		// Persistent mesh info buffer (read by main_build)
		std::unique_ptr<VulkanBuffer> meshInfoBuffer;
		uint32_t meshInfoCount = 0;
		uint32_t meshInfoCapacity = 0;

		// Per-frame true-batch buffers
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullMeshCountsBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullMeshOffsetsBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullMeshWriteCursorBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullGroupSumsBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullGroupOffsetsBuffers;
		std::vector<std::unique_ptr<VulkanBuffer>> perFrameCullVisibleInstanceBuffers;

		// Per-frame true-batch descriptor sets
		std::vector<VkDescriptorSet> perFrameTrueBatchCullDescriptorSets;

	};

}
