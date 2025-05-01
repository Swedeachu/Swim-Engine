#include "PCH.h"
#include "VulkanDescriptorManager.h"

namespace Engine
{

	VulkanDescriptorManager::VulkanDescriptorManager(VkDevice device, uint32_t maxSets, uint32_t maxBindlessTextures)
		: device(device), maxSets(maxSets), maxBindlessTextures(maxBindlessTextures)
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
		// Regular per-material layout: UBO (binding 0) + texture sampler (binding 1)
		VkDescriptorSetLayoutBinding uboBinding{};
		uboBinding.binding = 0;
		uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkDescriptorSetLayoutBinding samplerBinding{};
		samplerBinding.binding = 1;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboBinding, samplerBinding };

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

	void VulkanDescriptorManager::CreateUBODescriptorSet(VkBuffer buffer) 
	{
		VkDescriptorSetLayout uboLayout = GetLayout();
		VkDescriptorPool descriptorPool = GetPool();

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &uboLayout;

		// VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(device, &allocInfo, &uboDescriptorSet) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate UBO descriptor set!");
		}

		// Bind camera UBO at binding 0
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(CameraUBO);

		VkWriteDescriptorSet uboWrite{};
		uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		uboWrite.dstSet = uboDescriptorSet;
		uboWrite.dstBinding = 0;
		uboWrite.dstArrayElement = 0;
		uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboWrite.descriptorCount = 1;
		uboWrite.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(device, 1, &uboWrite, 0, nullptr);
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateSet(VkBuffer uniformBuffer, VkDeviceSize bufferSize, VkSampler sampler, VkImageView imageView)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptorSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate descriptor set!");
		}

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = uniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = bufferSize;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = sampler;
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descriptorSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = descriptorSet;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

		return descriptorSet;
	}

	void VulkanDescriptorManager::CreateBindlessLayout()
	{
		// Binding 0: Immutable sampler (non-variable)
		VkDescriptorSetLayoutBinding samplerBinding{};
		samplerBinding.binding = 0;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		samplerBinding.pImmutableSamplers = nullptr;

		// Binding 1: Bindless texture array (must be the highest binding index!)
		VkDescriptorSetLayoutBinding textureBinding{};
		textureBinding.binding = 1;
		textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		textureBinding.descriptorCount = maxBindlessTextures;
		textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		textureBinding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
				samplerBinding,    // binding = 0
				textureBinding     // binding = 1
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

	void VulkanDescriptorManager::UpdateBindlessTexture(uint32_t index, VkImageView imageView, VkSampler sampler)
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

	void VulkanDescriptorManager::SetBindlessSampler(VkSampler sampler)
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
