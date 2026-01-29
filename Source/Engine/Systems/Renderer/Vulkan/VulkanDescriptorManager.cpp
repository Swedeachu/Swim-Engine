#include "PCH.h"
#include "VulkanDescriptorManager.h"
#include <array>
#include <algorithm>
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
		CreatePool();
	}

	VulkanDescriptorManager::~VulkanDescriptorManager()
	{
		Cleanup();
	}

	void VulkanDescriptorManager::CreateLayout()
	{
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

	void VulkanDescriptorManager::CreatePool()
	{
		// Descriptor pool for per-frame sets
		std::array<VkDescriptorPoolSize, 2> poolSizes{};

		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = maxSets;

		poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[1].descriptorCount = maxSets * 5; // instance + ui + msdf + cullIndirect + cullCount

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

	// Create per-frame GPU-driven cull output buffers
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
		}
	}

	// Create one UBO and descriptor set per frame
	void VulkanDescriptorManager::CreatePerFrameUBOs(VkPhysicalDevice physicalDevice, uint32_t frameCount)
	{
		perFrameUBOs.resize(frameCount);
		perFrameInstanceBuffers.resize(frameCount);
		perFrameMeshDecoratorBuffers.resize(frameCount);
		perFrameMsdfBuffers.resize(frameCount);
		perFrameDescriptorSets.resize(frameCount);

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

			// Instance SSBO
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

			// MsdfTextGpuInstanceData SSBO (start big; can still grow later)
			constexpr int MAX_GLYPHS = 4000;
			const uint64_t msdf_ssbo_size = static_cast<uint64_t>(MAX_GLYPHS) * sizeof(MsdfTextGpuInstanceData);

			perFrameMsdfBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				msdf_ssbo_size,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			// Descriptor allocation
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			if (vkAllocateDescriptorSets(device, &allocInfo, &perFrameDescriptorSets[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to allocate per-frame descriptor set!");
			}

			// === UBO ===
			VkDescriptorBufferInfo uboInfo{};
			uboInfo.buffer = perFrameUBOs[i]->GetBuffer();
			uboInfo.offset = 0;
			uboInfo.range = sizeof(CameraUBO);

			VkWriteDescriptorSet uboWrite{};
			uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			uboWrite.dstSet = perFrameDescriptorSets[i];
			uboWrite.dstBinding = 0;
			uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			uboWrite.descriptorCount = 1;
			uboWrite.pBufferInfo = &uboInfo;

			// === Instance SSBO ===
			VkDescriptorBufferInfo instanceInfo{};
			instanceInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			instanceInfo.offset = 0;
			instanceInfo.range = perFrameInstanceBuffers[i]->GetSize();

			VkWriteDescriptorSet instanceWrite{};
			instanceWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			instanceWrite.dstSet = perFrameDescriptorSets[i];
			instanceWrite.dstBinding = 1;
			instanceWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			instanceWrite.descriptorCount = 1;
			instanceWrite.pBufferInfo = &instanceInfo;

			// === MeshDecorator SSBO ===
			VkDescriptorBufferInfo uiInfo{};
			uiInfo.buffer = perFrameMeshDecoratorBuffers[i]->GetBuffer();
			uiInfo.offset = 0;
			uiInfo.range = perFrameMeshDecoratorBuffers[i]->GetSize();

			VkWriteDescriptorSet uiWrite{};
			uiWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			uiWrite.dstSet = perFrameDescriptorSets[i];
			uiWrite.dstBinding = 2;
			uiWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			uiWrite.descriptorCount = 1;
			uiWrite.pBufferInfo = &uiInfo;

			// === MSDF SSBO ===
			VkDescriptorBufferInfo msdfInfo{};
			msdfInfo.buffer = perFrameMsdfBuffers[i]->GetBuffer();
			msdfInfo.offset = 0;
			msdfInfo.range = perFrameMsdfBuffers[i]->GetSize();

			VkWriteDescriptorSet msdfWrite{};
			msdfWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			msdfWrite.dstSet = perFrameDescriptorSets[i];
			msdfWrite.dstBinding = 3;
			msdfWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			msdfWrite.descriptorCount = 1;
			msdfWrite.pBufferInfo = &msdfInfo;

			// Optional cull buffer bindings (only if CreatePerFrameCullBuffers ran first)
			bool hasCullBuffers = (i < perFrameCullIndirectBuffers.size()) && (i < perFrameCullCountBuffers.size()) &&
				perFrameCullIndirectBuffers[i] && perFrameCullCountBuffers[i];

			if (hasCullBuffers)
			{
				// === Cull Indirect Output ===
				VkDescriptorBufferInfo indirectInfo{};
				indirectInfo.buffer = perFrameCullIndirectBuffers[i]->GetBuffer();
				indirectInfo.offset = 0;
				indirectInfo.range = perFrameCullIndirectBuffers[i]->GetSize();

				VkWriteDescriptorSet indirectWrite{};
				indirectWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				indirectWrite.dstSet = perFrameDescriptorSets[i];
				indirectWrite.dstBinding = 4;
				indirectWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				indirectWrite.descriptorCount = 1;
				indirectWrite.pBufferInfo = &indirectInfo;

				// === Cull Count Output ===
				VkDescriptorBufferInfo countInfo{};
				countInfo.buffer = perFrameCullCountBuffers[i]->GetBuffer();
				countInfo.offset = 0;
				countInfo.range = sizeof(uint32_t);

				VkWriteDescriptorSet countWrite{};
				countWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				countWrite.dstSet = perFrameDescriptorSets[i];
				countWrite.dstBinding = 5;
				countWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				countWrite.descriptorCount = 1;
				countWrite.pBufferInfo = &countInfo;

				std::array<VkWriteDescriptorSet, 6> writes = { uboWrite, instanceWrite, uiWrite, msdfWrite, indirectWrite, countWrite };
				vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
			}
			else
			{
				std::array<VkWriteDescriptorSet, 4> writes = { uboWrite, instanceWrite, uiWrite, msdfWrite };
				vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
			}
		}
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
		VkDescriptorBufferInfo info{};
		info.buffer = buffer.GetBuffer();
		info.offset = 0;
		info.range = buffer.GetSize();

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = perFrameDescriptorSets[frameIndex];
		write.dstBinding = dstBinding;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo = &info;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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

			// IMPORTANT: rewrite descriptor set binding to point at the new VkBuffer.
			RewritePerFrameStorageBinding(i, dstBinding, *buf);
		}
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
	}

}
