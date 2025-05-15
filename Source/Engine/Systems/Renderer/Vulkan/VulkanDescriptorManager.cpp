#include "PCH.h"
#include "VulkanDescriptorManager.h"

namespace Engine
{

	VulkanDescriptorManager::VulkanDescriptorManager(VkDevice device, uint32_t maxSets, uint32_t maxBindlessTextures, uint64_t ssbosSize)
		: device(device), maxSets(maxSets), maxBindlessTextures(maxBindlessTextures), ssboSize(ssbosSize)
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
		// Set 0: used in vertex shader for CameraUBO and instanceBuffer

		VkDescriptorSetLayoutBinding uboBinding{};
		uboBinding.binding = 0;
		uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		uboBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding instanceBufferBinding{};
		instanceBufferBinding.binding = 1;
		instanceBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; 
		instanceBufferBinding.descriptorCount = 1;
		instanceBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		instanceBufferBinding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
			uboBinding,
			instanceBufferBinding
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
		// Descriptor pool for regular per-object sets
		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = maxSets;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = maxSets;

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


	// Create one UBO and descriptor set per frame
	void VulkanDescriptorManager::CreatePerFrameUBOs(VkPhysicalDevice physicalDevice, uint32_t frameCount)
	{
		perFrameUBOs.resize(frameCount);
		perFrameInstanceBuffers.resize(frameCount);
		perFrameDescriptorSets.resize(frameCount);

		for (uint32_t i = 0; i < frameCount; ++i)
		{
			perFrameUBOs[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				sizeof(CameraUBO),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			perFrameInstanceBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				ssboSize,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			if (vkAllocateDescriptorSets(device, &allocInfo, &perFrameDescriptorSets[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to allocate per-frame descriptor set!");
			}

			// === UBO write ===
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

			// === SSBO write (instanceBuffer) ===
			VkDescriptorBufferInfo ssboInfo{};
			ssboInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			ssboInfo.offset = 0;
			ssboInfo.range = ssboSize;

			VkWriteDescriptorSet ssboWrite{};
			ssboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			ssboWrite.dstSet = perFrameDescriptorSets[i];
			ssboWrite.dstBinding = 1; // Binding 1 = storage buffer
			ssboWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			ssboWrite.descriptorCount = 1;
			ssboWrite.pBufferInfo = &ssboInfo;

			std::array<VkWriteDescriptorSet, 2> writes = { uboWrite, ssboWrite };
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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
			bufferInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = perFrameDescriptorSets[i]; // Reuse the existing descriptor sets created in CreatePerFrameUBOs
			write.dstBinding = 1;                     // Binding 1 = instance SSBO
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
			throw std::runtime_error("Invalid frame index for SSBO update");
		}

		perFrameInstanceBuffers[frameIndex]->CopyData(data, size);
	}

	// Update the UBO for a given frame with the latest camera matrix data
	void VulkanDescriptorManager::UpdatePerFrameUBO(uint32_t frameIndex, const CameraUBO& ubo)
	{
		if (frameIndex >= perFrameUBOs.size())
		{
			throw std::runtime_error("Invalid frame index for UBO update");
		}

		perFrameUBOs[frameIndex]->CopyData(&ubo, sizeof(CameraUBO));
	}

	// Get descriptor set for the active frame
	VkDescriptorSet VulkanDescriptorManager::GetPerFrameDescriptorSet(uint32_t frameIndex) const
	{
		return perFrameDescriptorSets.at(frameIndex);
	}

	void VulkanDescriptorManager::CreateBindlessLayout()
	{
		// Set 1: Used in FRAGMENT shader (also possibly vertex if needed in future)

		// Binding 0: Immutable sampler
		VkDescriptorSetLayoutBinding samplerBinding{};
		samplerBinding.binding = 0;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; 
		samplerBinding.pImmutableSamplers = nullptr;

		// Binding 1: Bindless image array
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
		// Only the first binding (binding 0: texture array) is variable-sized
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
		imageInfo.sampler = VK_NULL_HANDLE; // Required for VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = bindlessDescriptorSet;            
		write.dstBinding = 1; // texture array binding
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
		write.dstBinding = 0; // Binding 0: the sampler
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &samplerInfo;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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

		for (auto& buffer : perFrameInstanceBuffers)
		{
			if (buffer)
			{
				buffer->Free();
				buffer.reset();
			}
		}
		perFrameInstanceBuffers.clear();

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
