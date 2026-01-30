#include "PCH.h"
#include "VulkanDescriptorManager.h"
#include <array>
#include <algorithm>
#include <iostream>

#include "Buffers/VulkanGpuInstanceData.h"

namespace Engine
{

	VulkanDescriptorManager::VulkanDescriptorManager
	(
		VkDevice device,
		VkPhysicalDevice physicalDevice,
		uint32_t maxSets,
		uint32_t maxBindlessTextures,
		uint64_t ssbosSize
	)
		: device(device), physicalDevice(physicalDevice), maxSets(maxSets), maxBindlessTextures(maxBindlessTextures), ssboSize(ssbosSize)
	{
		CreateLayout();
		CreateTrueBatchCullLayout();
		CreatePool();
	}

	VulkanDescriptorManager::~VulkanDescriptorManager()
	{
		Cleanup();
	}

	void VulkanDescriptorManager::CreateLayout()
	{
		if (descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			descriptorSetLayout = VK_NULL_HANDLE;
		}

		VkDescriptorSetLayoutBinding uboBinding;
		uboBinding.binding = 0;
		uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
		uboBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding instanceBufferBinding;
		instanceBufferBinding.binding = 1;
		instanceBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		instanceBufferBinding.descriptorCount = 1;
		instanceBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
		instanceBufferBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding uiParamBufferBinding;
		uiParamBufferBinding.binding = 2;
		uiParamBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		uiParamBufferBinding.descriptorCount = 1;
		uiParamBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		uiParamBufferBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding msdfBufferBinding;
		msdfBufferBinding.binding = 3;
		msdfBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		msdfBufferBinding.descriptorCount = 1;
		msdfBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		msdfBufferBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding cullIndirectBinding;
		cullIndirectBinding.binding = 4;
		cullIndirectBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		cullIndirectBinding.descriptorCount = 1;
		cullIndirectBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		cullIndirectBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding cullCountBinding;
		cullCountBinding.binding = 5;
		cullCountBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		cullCountBinding.descriptorCount = 1;
		cullCountBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		cullCountBinding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 6> bindings = {
			uboBinding,
			instanceBufferBinding,
			uiParamBufferBinding,
			msdfBufferBinding,
			cullIndirectBinding,
			cullCountBinding
		};

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create descriptor set layout!");
		}
	}

	void VulkanDescriptorManager::CreateTrueBatchCullLayout()
	{
		if (trueBatchCullSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, trueBatchCullSetLayout, nullptr);
			trueBatchCullSetLayout = VK_NULL_HANDLE;
		}

		VkDescriptorSetLayoutBinding b0{};
		b0.binding = 0;
		b0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		b0.descriptorCount = 1;
		b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b0.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b1{};
		b1.binding = 1;
		b1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b1.descriptorCount = 1;
		b1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b1.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b2{};
		b2.binding = 2; // meshInfoBuffer
		b2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b2.descriptorCount = 1;
		b2.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b2.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b3{};
		b3.binding = 3; // meshCounts
		b3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b3.descriptorCount = 1;
		b3.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b3.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b4{};
		b4.binding = 4; // meshOffsets
		b4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b4.descriptorCount = 1;
		b4.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b4.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b5{};
		b5.binding = 5; // meshWriteCursor
		b5.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b5.descriptorCount = 1;
		b5.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b5.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b6{};
		b6.binding = 6; // groupSums
		b6.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b6.descriptorCount = 1;
		b6.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b6.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b7{};
		b7.binding = 7; // groupOffsets
		b7.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b7.descriptorCount = 1;
		b7.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b7.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b8{};
		b8.binding = 8; // visibleInstanceBuffer
		b8.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b8.descriptorCount = 1;
		b8.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b8.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b9{};
		b9.binding = 9; // outCommands
		b9.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b9.descriptorCount = 1;
		b9.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b9.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding b10{};
		b10.binding = 10; // outDrawCount
		b10.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		b10.descriptorCount = 1;
		b10.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		b10.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 11> bindings =
		{
			b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10
		};

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &trueBatchCullSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create true-batch cull descriptor set layout!");
		}
	}

	void VulkanDescriptorManager::CreatePool()
	{
		if (descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			descriptorPool = VK_NULL_HANDLE;
		}

		// Descriptor pool for per-frame sets and true-batch compute sets.
		// Keep this intentionally overprovisioned to avoid pool exhaustion during iteration.

		std::array<VkDescriptorPoolSize, 2> poolSizes{};

		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = maxSets * 2;

		poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[1].descriptorCount = maxSets * 32;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = maxSets;

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create descriptor pool!");
		}
	}

	// Create per-frame GPU-driven cull output buffers (legacy path).
	// True-batch reuses these as outCommands/outDrawCount.
	void VulkanDescriptorManager::CreatePerFrameCullBuffers(uint32_t frameCount, uint32_t maxCommands)
	{
		perFrameCullIndirectBuffers.resize(frameCount);
		perFrameCullCountBuffers.resize(frameCount);

		const VkDeviceSize indirectBytes = static_cast<VkDeviceSize>(maxCommands) * sizeof(VkDrawIndexedIndirectCommand);

		for (uint32_t i = 0; i < frameCount; ++i)
		{
			perFrameCullIndirectBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				indirectBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			perFrameCullCountBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(uint32_t),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			// If per-frame sets already exist, rewrite binding 4/5 to point at these buffers.
			RewritePerFrameCullBindings(i);
		}
	}

	// Create one UBO and descriptor set per frame
	void VulkanDescriptorManager::CreatePerFrameUBOs(VkPhysicalDevice physicalDevice, uint32_t frameCount)
	{
		this->frameCount = frameCount;

		perFrameUBOs.resize(frameCount);
		perFrameInstanceBuffers.resize(frameCount);
		perFrameMeshDecoratorBuffers.resize(frameCount);
		perFrameMsdfBuffers.resize(frameCount);

		perFrameDescriptorSets.resize(frameCount);
		perFrameCulledWorldDescriptorSets.resize(frameCount);

		for (uint32_t i = 0; i < frameCount; ++i)
		{
			// UBO
			perFrameUBOs[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(CameraUBO),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			// Instance SSBO (CPU/None path source data)
			perFrameInstanceBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				ssboSize,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			// MeshDecoratorGpuInstanceData SSBO
			perFrameMeshDecoratorBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				ssboSize,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			// MsdfTextGpuInstanceData SSBO
			constexpr int MAX_GLYPHS = 4000;
			const uint64_t msdf_ssbo_size = static_cast<uint64_t>(MAX_GLYPHS) * sizeof(MsdfTextGpuInstanceData);

			perFrameMsdfBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				msdf_ssbo_size,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			// Allocate standard set
			{
				VkDescriptorSetAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &descriptorSetLayout;

				if (vkAllocateDescriptorSets(device, &allocInfo, &perFrameDescriptorSets[i]) != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to allocate per-frame descriptor set!");
				}
			}

			// Allocate culled-world set (same layout)
			{
				VkDescriptorSetAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &descriptorSetLayout;

				if (vkAllocateDescriptorSets(device, &allocInfo, &perFrameCulledWorldDescriptorSets[i]) != VK_SUCCESS)
				{
					throw std::runtime_error("Failed to allocate per-frame culled-world descriptor set!");
				}
			}

			// Shared infos
			VkDescriptorBufferInfo uboInfo{};
			uboInfo.buffer = perFrameUBOs[i]->GetBuffer();
			uboInfo.offset = 0;
			uboInfo.range = sizeof(CameraUBO);

			VkDescriptorBufferInfo instanceInfo{};
			instanceInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			instanceInfo.offset = 0;
			instanceInfo.range = perFrameInstanceBuffers[i]->GetSize();

			VkDescriptorBufferInfo uiInfo{};
			uiInfo.buffer = perFrameMeshDecoratorBuffers[i]->GetBuffer();
			uiInfo.offset = 0;
			uiInfo.range = perFrameMeshDecoratorBuffers[i]->GetSize();

			VkDescriptorBufferInfo msdfInfo{};
			msdfInfo.buffer = perFrameMsdfBuffers[i]->GetBuffer();
			msdfInfo.offset = 0;
			msdfInfo.range = perFrameMsdfBuffers[i]->GetSize();

			// === Standard set writes (binding 1 = perFrameInstanceBuffers) ===
			{
				VkWriteDescriptorSet w0{};
				w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w0.dstSet = perFrameDescriptorSets[i];
				w0.dstBinding = 0;
				w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				w0.descriptorCount = 1;
				w0.pBufferInfo = &uboInfo;

				VkWriteDescriptorSet w1{};
				w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w1.dstSet = perFrameDescriptorSets[i];
				w1.dstBinding = 1;
				w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w1.descriptorCount = 1;
				w1.pBufferInfo = &instanceInfo;

				VkWriteDescriptorSet w2{};
				w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w2.dstSet = perFrameDescriptorSets[i];
				w2.dstBinding = 2;
				w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w2.descriptorCount = 1;
				w2.pBufferInfo = &uiInfo;

				VkWriteDescriptorSet w3{};
				w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w3.dstSet = perFrameDescriptorSets[i];
				w3.dstBinding = 3;
				w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w3.descriptorCount = 1;
				w3.pBufferInfo = &msdfInfo;

				// Optional cull outputs (bindings 4/5)
				bool hasCullBuffers = (i < perFrameCullIndirectBuffers.size()) && (i < perFrameCullCountBuffers.size()) &&
					perFrameCullIndirectBuffers[i] && perFrameCullCountBuffers[i];

				if (hasCullBuffers)
				{
					VkDescriptorBufferInfo indirectInfo{};
					indirectInfo.buffer = perFrameCullIndirectBuffers[i]->GetBuffer();
					indirectInfo.offset = 0;
					indirectInfo.range = perFrameCullIndirectBuffers[i]->GetSize();

					VkWriteDescriptorSet w4{};
					w4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					w4.dstSet = perFrameDescriptorSets[i];
					w4.dstBinding = 4;
					w4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					w4.descriptorCount = 1;
					w4.pBufferInfo = &indirectInfo;

					VkDescriptorBufferInfo countInfo{};
					countInfo.buffer = perFrameCullCountBuffers[i]->GetBuffer();
					countInfo.offset = 0;
					countInfo.range = sizeof(uint32_t);

					VkWriteDescriptorSet w5{};
					w5.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					w5.dstSet = perFrameDescriptorSets[i];
					w5.dstBinding = 5;
					w5.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					w5.descriptorCount = 1;
					w5.pBufferInfo = &countInfo;

					std::array<VkWriteDescriptorSet, 6> writes = { w0, w1, w2, w3, w4, w5 };
					vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
				}
				else
				{
					std::array<VkWriteDescriptorSet, 4> writes = { w0, w1, w2, w3 };
					vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
				}
			}

			// === Culled-world set writes (binding 1 = visibleInstanceBuffer when available) ===
			{
				VkDescriptorBufferInfo culledInstInfo = instanceInfo;

				if (i < perFrameCullVisibleInstanceBuffers.size() && perFrameCullVisibleInstanceBuffers[i])
				{
					culledInstInfo.buffer = perFrameCullVisibleInstanceBuffers[i]->GetBuffer();
					culledInstInfo.offset = 0;
					culledInstInfo.range = perFrameCullVisibleInstanceBuffers[i]->GetSize();
				}

				VkWriteDescriptorSet w0{};
				w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w0.dstSet = perFrameCulledWorldDescriptorSets[i];
				w0.dstBinding = 0;
				w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				w0.descriptorCount = 1;
				w0.pBufferInfo = &uboInfo;

				VkWriteDescriptorSet w1{};
				w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w1.dstSet = perFrameCulledWorldDescriptorSets[i];
				w1.dstBinding = 1;
				w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w1.descriptorCount = 1;
				w1.pBufferInfo = &culledInstInfo;

				VkWriteDescriptorSet w2{};
				w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w2.dstSet = perFrameCulledWorldDescriptorSets[i];
				w2.dstBinding = 2;
				w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w2.descriptorCount = 1;
				w2.pBufferInfo = &uiInfo;

				VkWriteDescriptorSet w3{};
				w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w3.dstSet = perFrameCulledWorldDescriptorSets[i];
				w3.dstBinding = 3;
				w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w3.descriptorCount = 1;
				w3.pBufferInfo = &msdfInfo;

				bool hasCullBuffers = (i < perFrameCullIndirectBuffers.size()) && (i < perFrameCullCountBuffers.size()) &&
					perFrameCullIndirectBuffers[i] && perFrameCullCountBuffers[i];

				if (hasCullBuffers)
				{
					VkDescriptorBufferInfo indirectInfo{};
					indirectInfo.buffer = perFrameCullIndirectBuffers[i]->GetBuffer();
					indirectInfo.offset = 0;
					indirectInfo.range = perFrameCullIndirectBuffers[i]->GetSize();

					VkWriteDescriptorSet w4{};
					w4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					w4.dstSet = perFrameCulledWorldDescriptorSets[i];
					w4.dstBinding = 4;
					w4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					w4.descriptorCount = 1;
					w4.pBufferInfo = &indirectInfo;

					VkDescriptorBufferInfo countInfo{};
					countInfo.buffer = perFrameCullCountBuffers[i]->GetBuffer();
					countInfo.offset = 0;
					countInfo.range = sizeof(uint32_t);

					VkWriteDescriptorSet w5{};
					w5.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					w5.dstSet = perFrameCulledWorldDescriptorSets[i];
					w5.dstBinding = 5;
					w5.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					w5.descriptorCount = 1;
					w5.pBufferInfo = &countInfo;

					std::array<VkWriteDescriptorSet, 6> writes = { w0, w1, w2, w3, w4, w5 };
					vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
				}
				else
				{
					std::array<VkWriteDescriptorSet, 4> writes = { w0, w1, w2, w3 };
					vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
				}
			}
		}

		AllocateTrueBatchDescriptorSetsIfNeeded();
	}

	VkDescriptorSet VulkanDescriptorManager::GetPerFrameCulledWorldDescriptorSet(uint32_t frameIndex) const
	{
		return perFrameCulledWorldDescriptorSets.at(frameIndex);
	}

	void VulkanDescriptorManager::CreateInstanceBufferDescriptorSets(const std::vector<std::unique_ptr<VulkanBuffer>>& perFrameInstanceBuffers)
	{
		const uint32_t frameCount = static_cast<uint32_t>(perFrameInstanceBuffers.size());

		for (uint32_t i = 0; i < frameCount; ++i)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = perFrameInstanceBuffers[i]->GetSize();

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = perFrameDescriptorSets[i];
			write.dstBinding = 1;
			write.dstArrayElement = 0;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write.descriptorCount = 1;
			write.pBufferInfo = &bufferInfo;

			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}
	}

	void VulkanDescriptorManager::UpdatePerFrameInstanceBuffer(uint32_t frameIndex, const void* data, size_t size)
	{
		if (frameIndex >= perFrameInstanceBuffers.size())
		{
			throw std::runtime_error("Invalid frame index for instance SSBO update");
		}

		perFrameInstanceBuffers[frameIndex]->CopyData(data, size);
	}

	void VulkanDescriptorManager::UpdatePerFrameUBO(uint32_t frameIndex, const CameraUBO& ubo)
	{
		if (frameIndex >= perFrameUBOs.size())
		{
			throw std::runtime_error("Invalid frame index for UBO update");
		}

		perFrameUBOs[frameIndex]->CopyData(&ubo, sizeof(CameraUBO));
	}

	VkDescriptorSet VulkanDescriptorManager::GetPerFrameDescriptorSet(uint32_t frameIndex) const
	{
		return perFrameDescriptorSets.at(frameIndex);
	}

	void VulkanDescriptorManager::UpdatePerFrameMeshDecoratorBuffer(uint32_t frameIndex, const void* data, size_t size)
	{
		if (frameIndex >= perFrameMeshDecoratorBuffers.size())
		{
			throw std::runtime_error("Invalid frame index for MeshDecorator SSBO update");
		}

		perFrameMeshDecoratorBuffers[frameIndex]->CopyData(data, size);
	}

	void VulkanDescriptorManager::UpdatePerFrameMsdfBuffer(uint32_t frameIndex, const void* data, size_t size)
	{
		if (frameIndex >= perFrameMsdfBuffers.size())
		{
			throw std::runtime_error("Invalid frame index for MSDF SSBO update");
		}

		perFrameMsdfBuffers[frameIndex]->CopyData(data, size);
	}

	VulkanBuffer* VulkanDescriptorManager::GetMeshDecoratorBufferForFrame(uint32_t frameIndex) const
	{
		return perFrameMeshDecoratorBuffers.at(frameIndex).get();
	}

	VulkanBuffer* VulkanDescriptorManager::GetMsdfBufferForFrame(uint32_t frameIndex) const
	{
		return perFrameMsdfBuffers.at(frameIndex).get();
	}

	VulkanBuffer* VulkanDescriptorManager::GetPerFrameUBO(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameUBOs.size())
		{
			throw std::runtime_error("Invalid frame index for UBO");
		}

		return perFrameUBOs[frameIndex].get();
	}

	VulkanBuffer* VulkanDescriptorManager::GetInstanceBufferForFrame(uint32_t frameIndex) const
	{
		return perFrameInstanceBuffers.at(frameIndex).get();
	}

	VkBuffer VulkanDescriptorManager::GetPerFrameCullCommandBuffer(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameCullIndirectBuffers.size() || !perFrameCullIndirectBuffers[frameIndex])
		{
			return VK_NULL_HANDLE;
		}

		return perFrameCullIndirectBuffers[frameIndex]->GetBuffer();
	}

	VkBuffer VulkanDescriptorManager::GetPerFrameCullCountBuffer(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameCullCountBuffers.size() || !perFrameCullCountBuffers[frameIndex])
		{
			return VK_NULL_HANDLE;
		}

		return perFrameCullCountBuffers[frameIndex]->GetBuffer();
	}

	VulkanBuffer* VulkanDescriptorManager::GetCullIndirectBufferForFrame(uint32_t frameIndex) const
	{
		return perFrameCullIndirectBuffers.at(frameIndex).get();
	}

	VulkanBuffer* VulkanDescriptorManager::GetCullCountBufferForFrame(uint32_t frameIndex) const
	{
		return perFrameCullCountBuffers.at(frameIndex).get();
	}

	void VulkanDescriptorManager::UpdateMeshInfo(uint32_t meshID, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
	{
		EnsureMeshInfoCapacity(meshID + 1u);

		GpuMeshInfo info{};
		info.indexCount = indexCount;
		info.firstIndex = firstIndex;
		info.vertexOffset = vertexOffset;
		info._pad0 = 0u;

		const size_t offsetBytes = static_cast<size_t>(meshID) * sizeof(GpuMeshInfo);
		meshInfoBuffer->CopyData(&info, sizeof(GpuMeshInfo), offsetBytes);

		if (meshID + 1u > meshInfoCount)
		{
			meshInfoCount = meshID + 1u;
		}
	}

	void VulkanDescriptorManager::UpdateMeshInfoBuffer(const void* data, size_t size, uint32_t meshCount)
	{
		if (meshCount == 0 || size == 0)
		{
			meshInfoCount = 0;

			if (meshInfoBuffer)
			{
				meshInfoBuffer->Free();
				meshInfoBuffer.reset();
			}

			meshInfoCapacity = 0;
			return;
		}

		// HOST_VISIBLE for easy updates (fits your VulkanBuffer design)
		if (!meshInfoBuffer || meshInfoBuffer->GetSize() < static_cast<VkDeviceSize>(size))
		{
			if (meshInfoBuffer)
			{
				meshInfoBuffer->Free();
				meshInfoBuffer.reset();
			}

			meshInfoBuffer = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				static_cast<VkDeviceSize>(size),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			meshInfoCapacity = static_cast<uint32_t>(size / sizeof(GpuMeshInfo));
		}

		meshInfoBuffer->CopyData(data, size);
		meshInfoCount = meshCount;

		// Compute sets reference meshInfoBuffer, so ensure binding(2) points at it
		WriteMeshInfoDescriptorSets();
	}

	uint32_t VulkanDescriptorManager::GetMeshInfoCount() const
	{
		return meshInfoCount;
	}

	void VulkanDescriptorManager::EnsureMeshInfoCapacity(uint32_t requiredMeshCount)
	{
		if (requiredMeshCount <= meshInfoCapacity)
		{
			return;
		}

		uint32_t newCap = 1u;
		while (newCap < requiredMeshCount)
		{
			newCap <<= 1u;
		}

		const VkDeviceSize newSize = static_cast<VkDeviceSize>(newCap) * sizeof(GpuMeshInfo);

		std::unique_ptr<VulkanBuffer> newBuf = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			newSize,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		// Preserve old contents if any (both are HOST_VISIBLE => persistently mapped)
		if (meshInfoBuffer && meshInfoCapacity > 0)
		{
			void* oldPtr = meshInfoBuffer->GetMappedPointer();
			void* newPtr = newBuf->GetMappedPointer();

			if (oldPtr && newPtr)
			{
				const VkDeviceSize copyBytes = static_cast<VkDeviceSize>(meshInfoCapacity) * sizeof(GpuMeshInfo);
				memcpy(newPtr, oldPtr, static_cast<size_t>(copyBytes));
			}
		}

		if (meshInfoBuffer)
		{
			meshInfoBuffer->Free();
			meshInfoBuffer.reset();
		}

		meshInfoBuffer = std::move(newBuf);
		meshInfoCapacity = newCap;

		// After realloc, compute sets must point at the new meshInfoBuffer
		WriteMeshInfoDescriptorSets();
	}

	void VulkanDescriptorManager::WriteMeshInfoDescriptorSets()
	{
		if (!meshInfoBuffer)
		{
			return;
		}

		VkDescriptorBufferInfo meshInfoBI{};
		meshInfoBI.buffer = meshInfoBuffer->GetBuffer();
		meshInfoBI.offset = 0;
		meshInfoBI.range = meshInfoBuffer->GetSize();

		for (uint32_t i = 0; i < static_cast<uint32_t>(perFrameTrueBatchCullDescriptorSets.size()); i++)
		{
			VkDescriptorSet set = perFrameTrueBatchCullDescriptorSets[i];
			if (set == VK_NULL_HANDLE)
			{
				continue;
			}

			VkWriteDescriptorSet w{};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = set;
			w.dstBinding = 2; // shader: [[vk::binding(2,0)]] meshInfoBuffer
			w.dstArrayElement = 0;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w.pBufferInfo = &meshInfoBI;

			vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
		}
	}

	void VulkanDescriptorManager::EnsurePerFrameTrueBatchCullCapacity(uint32_t frameIndex, uint32_t instanceCount, uint32_t meshCount, uint32_t meshGroupCount)
	{
		if (frameIndex >= frameCount)
		{
			throw std::runtime_error("EnsurePerFrameTrueBatchCullCapacity: invalid frame index");
		}

		// Ensure legacy outputs exist (true-batch writes outCommands/outDrawCount into these).
		if (frameIndex >= perFrameCullIndirectBuffers.size() || frameIndex >= perFrameCullCountBuffers.size() ||
			!perFrameCullIndirectBuffers[frameIndex] || !perFrameCullCountBuffers[frameIndex])
		{
			if (perFrameCullIndirectBuffers.size() != frameCount || perFrameCullCountBuffers.size() != frameCount)
			{
				CreatePerFrameCullBuffers(frameCount, std::max<uint32_t>(meshCount, 1));
			}

			RewritePerFrameCullBindings(frameIndex);
		}
		else
		{
			const VkDeviceSize needIndirect = static_cast<VkDeviceSize>(std::max<uint32_t>(meshCount, 1)) * sizeof(VkDrawIndexedIndirectCommand);
			if (perFrameCullIndirectBuffers[frameIndex]->GetSize() < needIndirect)
			{
				std::cout << "EnsurePerFrameTrueBatchCullCapacity | Grow indirect buffer (frame " << frameIndex << ")" << std::endl;

				perFrameCullIndirectBuffers[frameIndex]->Free();
				perFrameCullIndirectBuffers[frameIndex] = std::make_unique<VulkanBuffer>(
					device,
					physicalDevice,
					needIndirect,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
				);

				RewritePerFrameCullBindings(frameIndex);
			}
		}

		if (perFrameCullMeshCountsBuffers.size() != frameCount)
		{
			perFrameCullMeshCountsBuffers.resize(frameCount);
			perFrameCullMeshOffsetsBuffers.resize(frameCount);
			perFrameCullMeshWriteCursorBuffers.resize(frameCount);
			perFrameCullGroupSumsBuffers.resize(frameCount);
			perFrameCullGroupOffsetsBuffers.resize(frameCount);
			perFrameCullVisibleInstanceBuffers.resize(frameCount);
		}

		const VkDeviceSize meshBytes = static_cast<VkDeviceSize>(std::max<uint32_t>(meshCount, 1)) * sizeof(uint32_t);
		const VkDeviceSize groupBytes = static_cast<VkDeviceSize>(std::max<uint32_t>(meshGroupCount, 1)) * sizeof(uint32_t);

		EnsureDeviceLocalBuffer(perFrameCullMeshCountsBuffers[frameIndex], meshBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		EnsureDeviceLocalBuffer(perFrameCullMeshOffsetsBuffers[frameIndex], meshBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		EnsureDeviceLocalBuffer(perFrameCullMeshWriteCursorBuffers[frameIndex], meshBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		EnsureDeviceLocalBuffer(perFrameCullGroupSumsBuffers[frameIndex], groupBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		EnsureDeviceLocalBuffer(perFrameCullGroupOffsetsBuffers[frameIndex], groupBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		const VkDeviceSize visBytes = static_cast<VkDeviceSize>(std::max<uint32_t>(instanceCount, 1)) * sizeof(GpuInstanceData);

		EnsureDeviceLocalBuffer(perFrameCullVisibleInstanceBuffers[frameIndex], visBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		if (!meshInfoBuffer)
		{
			meshInfoBuffer = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(uint32_t),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			uint32_t zero = 0;
			meshInfoBuffer->CopyData(&zero, sizeof(uint32_t));
		}

		AllocateTrueBatchDescriptorSetsIfNeeded();
		UpdateTrueBatchDescriptorSetForFrame(frameIndex);

		// IMPORTANT: gfx shader reads instanceBuffer (set0/binding1) via SV_InstanceID.
		// In GPU culled mode, SV_InstanceID must index visibleInstanceBuffer, so bind that here.
		RewriteCulledWorldInstanceBinding(frameIndex);
	}

	VkDescriptorSet VulkanDescriptorManager::GetPerFrameTrueBatchCullDescriptorSet(uint32_t frameIndex) const
	{
		return perFrameTrueBatchCullDescriptorSets.at(frameIndex);
	}

	VkBuffer VulkanDescriptorManager::GetPerFrameCullMeshCountsBuffer(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameCullMeshCountsBuffers.size() || !perFrameCullMeshCountsBuffers[frameIndex])
		{
			return VK_NULL_HANDLE;
		}

		return perFrameCullMeshCountsBuffers[frameIndex]->GetBuffer();
	}

	VkBuffer VulkanDescriptorManager::GetPerFrameCullMeshOffsetsBuffer(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameCullMeshOffsetsBuffers.size() || !perFrameCullMeshOffsetsBuffers[frameIndex])
		{
			return VK_NULL_HANDLE;
		}

		return perFrameCullMeshOffsetsBuffers[frameIndex]->GetBuffer();
	}

	VkBuffer VulkanDescriptorManager::GetPerFrameCullMeshWriteCursorBuffer(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameCullMeshWriteCursorBuffers.size() || !perFrameCullMeshWriteCursorBuffers[frameIndex])
		{
			return VK_NULL_HANDLE;
		}

		return perFrameCullMeshWriteCursorBuffers[frameIndex]->GetBuffer();
	}

	VkBuffer VulkanDescriptorManager::GetPerFrameCullGroupSumsBuffer(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameCullGroupSumsBuffers.size() || !perFrameCullGroupSumsBuffers[frameIndex])
		{
			return VK_NULL_HANDLE;
		}

		return perFrameCullGroupSumsBuffers[frameIndex]->GetBuffer();
	}

	VkBuffer VulkanDescriptorManager::GetPerFrameCullGroupOffsetsBuffer(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameCullGroupOffsetsBuffers.size() || !perFrameCullGroupOffsetsBuffers[frameIndex])
		{
			return VK_NULL_HANDLE;
		}

		return perFrameCullGroupOffsetsBuffers[frameIndex]->GetBuffer();
	}

	VkBuffer VulkanDescriptorManager::GetPerFrameCullVisibleInstanceBuffer(uint32_t frameIndex) const
	{
		if (frameIndex >= perFrameCullVisibleInstanceBuffers.size() || !perFrameCullVisibleInstanceBuffers[frameIndex])
		{
			return VK_NULL_HANDLE;
		}

		return perFrameCullVisibleInstanceBuffers[frameIndex]->GetBuffer();
	}

	void VulkanDescriptorManager::AllocateTrueBatchDescriptorSetsIfNeeded()
	{
		if (frameCount == 0)
		{
			return;
		}

		if (perFrameTrueBatchCullDescriptorSets.size() == frameCount)
		{
			// If we already have valid sets, nothing to do.
			bool anyNull = false;
			for (uint32_t i = 0; i < frameCount; ++i)
			{
				if (perFrameTrueBatchCullDescriptorSets[i] == VK_NULL_HANDLE)
				{
					anyNull = true;
					break;
				}
			}

			if (!anyNull)
			{
				return;
			}
		}

		perFrameTrueBatchCullDescriptorSets.resize(frameCount);

		for (uint32_t i = 0; i < frameCount; ++i)
		{
			if (perFrameTrueBatchCullDescriptorSets[i] != VK_NULL_HANDLE)
			{
				continue;
			}

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &trueBatchCullSetLayout;

			if (vkAllocateDescriptorSets(device, &allocInfo, &perFrameTrueBatchCullDescriptorSets[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to allocate true-batch cull descriptor set!");
			}
		}
	}

	void VulkanDescriptorManager::UpdateTrueBatchDescriptorSetForFrame(uint32_t frameIndex)
	{
		if (frameIndex >= frameCount)
		{
			throw std::runtime_error("UpdateTrueBatchDescriptorSetForFrame: invalid frame index");
		}

		VkDescriptorSet set = perFrameTrueBatchCullDescriptorSets.at(frameIndex);
		if (set == VK_NULL_HANDLE)
		{
			return;
		}

		if (!perFrameUBOs[frameIndex] || !perFrameInstanceBuffers[frameIndex])
		{
			return;
		}

		if (frameIndex >= perFrameCullMeshCountsBuffers.size() ||
			!perFrameCullMeshCountsBuffers[frameIndex] ||
			!perFrameCullMeshOffsetsBuffers[frameIndex] ||
			!perFrameCullMeshWriteCursorBuffers[frameIndex] ||
			!perFrameCullGroupSumsBuffers[frameIndex] ||
			!perFrameCullGroupOffsetsBuffers[frameIndex] ||
			!perFrameCullVisibleInstanceBuffers[frameIndex])
		{
			return;
		}

		if (frameIndex >= perFrameCullIndirectBuffers.size() ||
			frameIndex >= perFrameCullCountBuffers.size() ||
			!perFrameCullIndirectBuffers[frameIndex] ||
			!perFrameCullCountBuffers[frameIndex])
		{
			return;
		}

		if (!meshInfoBuffer)
		{
			return;
		}

		// 0: UBO
		VkDescriptorBufferInfo uboInfo{};
		uboInfo.buffer = perFrameUBOs[frameIndex]->GetBuffer();
		uboInfo.offset = 0;
		uboInfo.range = sizeof(CameraUBO);

		VkWriteDescriptorSet w0{};
		w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w0.dstSet = set;
		w0.dstBinding = 0;
		w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		w0.descriptorCount = 1;
		w0.pBufferInfo = &uboInfo;

		// 1: instanceBuffer
		VkDescriptorBufferInfo instInfo{};
		instInfo.buffer = perFrameInstanceBuffers[frameIndex]->GetBuffer();
		instInfo.offset = 0;
		instInfo.range = perFrameInstanceBuffers[frameIndex]->GetSize();

		VkWriteDescriptorSet w1{};
		w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w1.dstSet = set;
		w1.dstBinding = 1;
		w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w1.descriptorCount = 1;
		w1.pBufferInfo = &instInfo;

		// 2: meshInfoBuffer
		VkDescriptorBufferInfo meshInfo{};
		meshInfo.buffer = meshInfoBuffer->GetBuffer();
		meshInfo.offset = 0;
		meshInfo.range = meshInfoBuffer->GetSize();

		VkWriteDescriptorSet w2{};
		w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w2.dstSet = set;
		w2.dstBinding = 2;
		w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w2.descriptorCount = 1;
		w2.pBufferInfo = &meshInfo;

		// 3: meshCounts
		VkDescriptorBufferInfo countsInfo{};
		countsInfo.buffer = perFrameCullMeshCountsBuffers[frameIndex]->GetBuffer();
		countsInfo.offset = 0;
		countsInfo.range = perFrameCullMeshCountsBuffers[frameIndex]->GetSize();

		VkWriteDescriptorSet w3{};
		w3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w3.dstSet = set;
		w3.dstBinding = 3;
		w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w3.descriptorCount = 1;
		w3.pBufferInfo = &countsInfo;

		// 4: meshOffsets
		VkDescriptorBufferInfo offsetsInfo{};
		offsetsInfo.buffer = perFrameCullMeshOffsetsBuffers[frameIndex]->GetBuffer();
		offsetsInfo.offset = 0;
		offsetsInfo.range = perFrameCullMeshOffsetsBuffers[frameIndex]->GetSize();

		VkWriteDescriptorSet w4{};
		w4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w4.dstSet = set;
		w4.dstBinding = 4;
		w4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w4.descriptorCount = 1;
		w4.pBufferInfo = &offsetsInfo;

		// 5: meshWriteCursor
		VkDescriptorBufferInfo cursorInfo{};
		cursorInfo.buffer = perFrameCullMeshWriteCursorBuffers[frameIndex]->GetBuffer();
		cursorInfo.offset = 0;
		cursorInfo.range = perFrameCullMeshWriteCursorBuffers[frameIndex]->GetSize();

		VkWriteDescriptorSet w5{};
		w5.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w5.dstSet = set;
		w5.dstBinding = 5;
		w5.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w5.descriptorCount = 1;
		w5.pBufferInfo = &cursorInfo;

		// 6: groupSums
		VkDescriptorBufferInfo groupSumsInfo{};
		groupSumsInfo.buffer = perFrameCullGroupSumsBuffers[frameIndex]->GetBuffer();
		groupSumsInfo.offset = 0;
		groupSumsInfo.range = perFrameCullGroupSumsBuffers[frameIndex]->GetSize();

		VkWriteDescriptorSet w6{};
		w6.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w6.dstSet = set;
		w6.dstBinding = 6;
		w6.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w6.descriptorCount = 1;
		w6.pBufferInfo = &groupSumsInfo;

		// 7: groupOffsets
		VkDescriptorBufferInfo groupOffsetsInfo{};
		groupOffsetsInfo.buffer = perFrameCullGroupOffsetsBuffers[frameIndex]->GetBuffer();
		groupOffsetsInfo.offset = 0;
		groupOffsetsInfo.range = perFrameCullGroupOffsetsBuffers[frameIndex]->GetSize();

		VkWriteDescriptorSet w7{};
		w7.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w7.dstSet = set;
		w7.dstBinding = 7;
		w7.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w7.descriptorCount = 1;
		w7.pBufferInfo = &groupOffsetsInfo;

		// 8: visibleInstanceBuffer
		VkDescriptorBufferInfo visInfo{};
		visInfo.buffer = perFrameCullVisibleInstanceBuffers[frameIndex]->GetBuffer();
		visInfo.offset = 0;
		visInfo.range = perFrameCullVisibleInstanceBuffers[frameIndex]->GetSize();

		VkWriteDescriptorSet w8{};
		w8.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w8.dstSet = set;
		w8.dstBinding = 8;
		w8.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w8.descriptorCount = 1;
		w8.pBufferInfo = &visInfo;

		// 9: outCommands (legacy cull indirect buffer)
		VkDescriptorBufferInfo outCmdInfo{};
		outCmdInfo.buffer = perFrameCullIndirectBuffers[frameIndex]->GetBuffer();
		outCmdInfo.offset = 0;
		outCmdInfo.range = perFrameCullIndirectBuffers[frameIndex]->GetSize();

		VkWriteDescriptorSet w9{};
		w9.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w9.dstSet = set;
		w9.dstBinding = 9;
		w9.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w9.descriptorCount = 1;
		w9.pBufferInfo = &outCmdInfo;

		// 10: outDrawCount (legacy cull count buffer)
		VkDescriptorBufferInfo outCountInfo{};
		outCountInfo.buffer = perFrameCullCountBuffers[frameIndex]->GetBuffer();
		outCountInfo.offset = 0;
		outCountInfo.range = sizeof(uint32_t);

		VkWriteDescriptorSet w10{};
		w10.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w10.dstSet = set;
		w10.dstBinding = 10;
		w10.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w10.descriptorCount = 1;
		w10.pBufferInfo = &outCountInfo;

		std::array<VkWriteDescriptorSet, 11> writes = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

	void VulkanDescriptorManager::EnsureDeviceLocalBuffer(std::unique_ptr<VulkanBuffer>& buf, VkDeviceSize bytes, VkBufferUsageFlags usage)
	{
		if (bytes == 0)
		{
			bytes = sizeof(uint32_t);
		}

		if (buf && buf->GetSize() >= bytes)
		{
			return;
		}

		if (buf)
		{
			buf->Free();
			buf.reset();
		}

		buf = std::make_unique<VulkanBuffer>(
			device,
			physicalDevice,
			bytes,
			usage,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
	}

	void VulkanDescriptorManager::EnsurePerFrameInstanceCapacity(size_t bytes)
	{
		EnsurePerFrameBufferCapacity(bytes, perFrameInstanceBuffers, 1);
	}

	void VulkanDescriptorManager::EnsurePerFrameMeshDecoratorCapacity(size_t bytes)
	{
		EnsurePerFrameBufferCapacity(bytes, perFrameMeshDecoratorBuffers, 2);
	}

	void VulkanDescriptorManager::EnsurePerFrameMsdfCapacity(size_t bytes)
	{
		EnsurePerFrameBufferCapacity(bytes, perFrameMsdfBuffers, 3);
	}

	void VulkanDescriptorManager::RewritePerFrameStorageBinding(uint32_t frameIndex, uint32_t dstBinding, VulkanBuffer& buffer)
	{
		RewritePerFrameStorageBinding(perFrameDescriptorSets[frameIndex], dstBinding, buffer);
	}

	void VulkanDescriptorManager::RewritePerFrameStorageBinding(VkDescriptorSet dstSet, uint32_t dstBinding, VulkanBuffer& buffer)
	{
		VkDescriptorBufferInfo info{};
		info.buffer = buffer.GetBuffer();
		info.offset = 0;
		info.range = buffer.GetSize();

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = dstSet;
		write.dstBinding = dstBinding;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo = &info;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	void VulkanDescriptorManager::RewriteCulledWorldInstanceBinding(uint32_t frameIndex)
	{
		if (frameIndex >= perFrameCulledWorldDescriptorSets.size())
		{
			return;
		}

		if (frameIndex >= perFrameCullVisibleInstanceBuffers.size())
		{
			return;
		}

		if (!perFrameCullVisibleInstanceBuffers[frameIndex])
		{
			return;
		}

		RewritePerFrameStorageBinding(perFrameCulledWorldDescriptorSets[frameIndex], 1, *perFrameCullVisibleInstanceBuffers[frameIndex]);
	}

	void VulkanDescriptorManager::RewritePerFrameCullBindings(uint32_t frameIndex)
	{
		if (frameIndex >= perFrameDescriptorSets.size() || frameIndex >= perFrameCulledWorldDescriptorSets.size())
		{
			return;
		}

		if (frameIndex >= perFrameCullIndirectBuffers.size() || frameIndex >= perFrameCullCountBuffers.size())
		{
			return;
		}

		if (!perFrameCullIndirectBuffers[frameIndex] || !perFrameCullCountBuffers[frameIndex])
		{
			return;
		}

		RewritePerFrameStorageBinding(perFrameDescriptorSets[frameIndex], 4, *perFrameCullIndirectBuffers[frameIndex]);
		RewritePerFrameStorageBinding(perFrameDescriptorSets[frameIndex], 5, *perFrameCullCountBuffers[frameIndex]);

		RewritePerFrameStorageBinding(perFrameCulledWorldDescriptorSets[frameIndex], 4, *perFrameCullIndirectBuffers[frameIndex]);
		RewritePerFrameStorageBinding(perFrameCulledWorldDescriptorSets[frameIndex], 5, *perFrameCullCountBuffers[frameIndex]);
	}

	void VulkanDescriptorManager::EnsurePerFrameBufferCapacity
	(
		size_t bytes,
		std::vector<std::unique_ptr<VulkanBuffer>>& buffers,
		uint32_t dstBinding
	)
	{
		for (uint32_t i = 0; i < buffers.size(); ++i)
		{
			auto& buf = buffers[i];
			if (buf->GetSize() >= bytes)
			{
				continue;
			}

			std::cout << "EnsurePerFrameBufferCapacity | Need to grow buffer (binding " << dstBinding << ")" << std::endl;

			const VkDeviceSize newSize = std::max<VkDeviceSize>(bytes, buf->GetSize() * 2);

			std::unique_ptr<VulkanBuffer> newBuf = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				newSize,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			buf->Free();
			buf = std::move(newBuf);

			// Rewrite standard set binding
			RewritePerFrameStorageBinding(perFrameDescriptorSets[i], dstBinding, *buf);

			// For bindings that are shared by culled-world set (2 = decorator, 3 = msdf), rewrite those too.
			if (dstBinding == 2 || dstBinding == 3)
			{
				RewritePerFrameStorageBinding(perFrameCulledWorldDescriptorSets[i], dstBinding, *buf);
			}
		}
	}

	void VulkanDescriptorManager::CreateBindlessLayout()
	{
		// Set 1: Used in FRAGMENT shader

		// Binding 0: sampler
		VkDescriptorSetLayoutBinding samplerBinding{};
		samplerBinding.binding = 0;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		samplerBinding.pImmutableSamplers = nullptr;

		// Binding 1: bindless image array
		VkDescriptorSetLayoutBinding textureBinding{};
		textureBinding.binding = 1;
		textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		textureBinding.descriptorCount = maxBindlessTextures;
		textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		textureBinding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
			samplerBinding,
			textureBinding
		};

		std::array<VkDescriptorBindingFlags, 2> bindingFlags{};
		bindingFlags[0] = 0;
		bindingFlags[1] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
		extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		extendedInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
		extendedInfo.pBindingFlags = bindingFlags.data();

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		layoutInfo.pNext = &extendedInfo;

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &bindlessSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create bindless descriptor set layout!");
		}
	}

	void VulkanDescriptorManager::CreateBindlessPool()
	{
		std::array<VkDescriptorPoolSize, 2> poolSizes{};

		poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		poolSizes[0].descriptorCount = maxBindlessTextures;

		poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
		poolSizes[1].descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &bindlessDescriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create bindless descriptor pool!");
		}
	}

	void VulkanDescriptorManager::AllocateBindlessSet()
	{
		uint32_t variableDescriptorCount = maxBindlessTextures;

		VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
		countInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
		countInfo.descriptorSetCount = 1;
		countInfo.pDescriptorCounts = &variableDescriptorCount;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = bindlessDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &bindlessSetLayout;
		allocInfo.pNext = &countInfo;

		if (vkAllocateDescriptorSets(device, &allocInfo, &bindlessDescriptorSet) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate bindless descriptor set!");
		}
	}

	void VulkanDescriptorManager::UpdateBindlessTexture(uint32_t index, VkImageView imageView, VkSampler sampler) const
	{
		if (imageView == VK_NULL_HANDLE)
		{
			throw std::runtime_error("UpdateBindlessTexture: imageView is null!");
		}

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = VK_NULL_HANDLE;
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = bindlessDescriptorSet;
		write.dstBinding = 1;
		write.dstArrayElement = index;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	void VulkanDescriptorManager::SetBindlessSampler(VkSampler sampler) const
	{
		VkDescriptorImageInfo samplerInfo{};
		samplerInfo.sampler = sampler;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = bindlessDescriptorSet;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &samplerInfo;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	void VulkanDescriptorManager::RewriteTrueBatchInstanceBinding(uint32_t frameIndex, VulkanBuffer& buffer)
	{
		if (frameIndex >= perFrameTrueBatchCullDescriptorSets.size())
		{
			return;
		}

		VkDescriptorSet set = perFrameTrueBatchCullDescriptorSets[frameIndex];
		if (set == VK_NULL_HANDLE)
		{
			return;
		}

		RewritePerFrameStorageBinding(set, 1, buffer);
	}

	void VulkanDescriptorManager::Cleanup()
	{
		for (auto& buffer : perFrameUBOs)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}

		perFrameUBOs.clear();
		perFrameDescriptorSets.clear();
		perFrameCulledWorldDescriptorSets.clear();

		for (auto& buffer : perFrameMeshDecoratorBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameMeshDecoratorBuffers.clear();

		for (auto& buffer : perFrameInstanceBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameInstanceBuffers.clear();

		for (auto& buffer : perFrameMsdfBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameMsdfBuffers.clear();

		for (auto& buffer : perFrameCullIndirectBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameCullIndirectBuffers.clear();

		for (auto& buffer : perFrameCullCountBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameCullCountBuffers.clear();

		for (auto& buffer : perFrameCullMeshCountsBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameCullMeshCountsBuffers.clear();

		for (auto& buffer : perFrameCullMeshOffsetsBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameCullMeshOffsetsBuffers.clear();

		for (auto& buffer : perFrameCullMeshWriteCursorBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameCullMeshWriteCursorBuffers.clear();

		for (auto& buffer : perFrameCullGroupSumsBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameCullGroupSumsBuffers.clear();

		for (auto& buffer : perFrameCullGroupOffsetsBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameCullGroupOffsetsBuffers.clear();

		for (auto& buffer : perFrameCullVisibleInstanceBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameCullVisibleInstanceBuffers.clear();

		perFrameTrueBatchCullDescriptorSets.clear();

		if (meshInfoBuffer)
		{
			meshInfoBuffer->Free();
			meshInfoBuffer.reset();
		}
		meshInfoCount = 0;

		if (bindlessDescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, bindlessDescriptorPool, nullptr);
			bindlessDescriptorPool = VK_NULL_HANDLE;
		}

		if (bindlessSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, bindlessSetLayout, nullptr);
			bindlessSetLayout = VK_NULL_HANDLE;
		}

		bindlessDescriptorSet = VK_NULL_HANDLE;

		if (descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
			descriptorPool = VK_NULL_HANDLE;
		}

		if (descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			descriptorSetLayout = VK_NULL_HANDLE;
		}

		if (trueBatchCullSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, trueBatchCullSetLayout, nullptr);
			trueBatchCullSetLayout = VK_NULL_HANDLE;
		}

		frameCount = 0;
	}

}
