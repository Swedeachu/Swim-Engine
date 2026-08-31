#include "PCH.h"
#include "VulkanDescriptorManager.h"
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
		VkDescriptorSetLayoutBinding uboBinding{};
		uboBinding.binding = 0;
		uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		uboBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding instanceBufferBinding{};
		instanceBufferBinding.binding = 1;
		instanceBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		instanceBufferBinding.descriptorCount = 1;
		instanceBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		instanceBufferBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding uiParamBufferBinding{};
		uiParamBufferBinding.binding = 2;
		uiParamBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		uiParamBufferBinding.descriptorCount = 1;
		uiParamBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		uiParamBufferBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding msdfBufferBinding{};
		msdfBufferBinding.binding = 3;
		msdfBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		msdfBufferBinding.descriptorCount = 1;
		msdfBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		msdfBufferBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding worldInstanceBufferBinding{};
		worldInstanceBufferBinding.binding = 4;
		worldInstanceBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		worldInstanceBufferBinding.descriptorCount = 1;
		worldInstanceBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		worldInstanceBufferBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding gpuWorldStaticBinding{};
		gpuWorldStaticBinding.binding = 5;
		gpuWorldStaticBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		gpuWorldStaticBinding.descriptorCount = 1;
		gpuWorldStaticBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		gpuWorldStaticBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding gpuWorldTransformBinding{};
		gpuWorldTransformBinding.binding = 6;
		gpuWorldTransformBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		gpuWorldTransformBinding.descriptorCount = 1;
		gpuWorldTransformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		gpuWorldTransformBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding gpuWorldVisibleIndexBinding{};
		gpuWorldVisibleIndexBinding.binding = 7;
		gpuWorldVisibleIndexBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		gpuWorldVisibleIndexBinding.descriptorCount = 1;
		gpuWorldVisibleIndexBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		gpuWorldVisibleIndexBinding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 8> bindings = {
			uboBinding,
			instanceBufferBinding,
			uiParamBufferBinding,
			msdfBufferBinding,
			worldInstanceBufferBinding,
			gpuWorldStaticBinding,
			gpuWorldTransformBinding,
			gpuWorldVisibleIndexBinding
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
		std::array<VkDescriptorPoolSize, 3> poolSizes{};

		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = maxSets;

		poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[1].descriptorCount = maxSets * 7;

		poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[2].descriptorCount = maxSets;

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

	void VulkanDescriptorManager::CreatePerFrameUBOs(VkPhysicalDevice physicalDevice, uint32_t frameCount)
	{
		perFrameUBOs.resize(frameCount);
		perFrameInstanceBuffers.resize(frameCount);
		perFrameMeshDecoratorBuffers.resize(frameCount);
		perFrameMsdfBuffers.resize(frameCount);
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

			perFrameMeshDecoratorBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				ssboSize,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			constexpr int MAX_GLYPHS = 4000;
			const uint64_t msdf_ssbo_size = static_cast<uint64_t>(MAX_GLYPHS) * sizeof(MsdfTextGpuInstanceData);
			perFrameMsdfBuffers[i] = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				msdf_ssbo_size,
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

			VkDescriptorBufferInfo uboInfo{};
			uboInfo.buffer = perFrameUBOs[i]->GetBuffer();
			uboInfo.offset = 0;
			uboInfo.range = sizeof(CameraUBO);

			VkDescriptorBufferInfo instanceInfo{};
			instanceInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			instanceInfo.offset = 0;
			instanceInfo.range = ssboSize;

			VkDescriptorBufferInfo uiInfo{};
			uiInfo.buffer = perFrameMeshDecoratorBuffers[i]->GetBuffer();
			uiInfo.offset = 0;
			uiInfo.range = ssboSize;

			VkDescriptorBufferInfo msdfInfo{};
			msdfInfo.buffer = perFrameMsdfBuffers[i]->GetBuffer();
			msdfInfo.offset = 0;
			msdfInfo.range = perFrameMsdfBuffers[i]->GetSize();

			VkDescriptorBufferInfo worldInstanceInfo{};
			worldInstanceInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			worldInstanceInfo.offset = 0;
			worldInstanceInfo.range = ssboSize;

			VkDescriptorBufferInfo gpuWorldStaticInfo{};
			gpuWorldStaticInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			gpuWorldStaticInfo.offset = 0;
			gpuWorldStaticInfo.range = ssboSize;

			VkDescriptorBufferInfo gpuWorldTransformInfo{};
			gpuWorldTransformInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			gpuWorldTransformInfo.offset = 0;
			gpuWorldTransformInfo.range = ssboSize;

			VkDescriptorBufferInfo gpuWorldVisibleIndexInfo{};
			gpuWorldVisibleIndexInfo.buffer = perFrameInstanceBuffers[i]->GetBuffer();
			gpuWorldVisibleIndexInfo.offset = 0;
			gpuWorldVisibleIndexInfo.range = ssboSize;

			std::array<VkWriteDescriptorSet, 8> writes{};
			for (uint32_t writeIndex = 0; writeIndex < static_cast<uint32_t>(writes.size()); ++writeIndex)
			{
				writes[writeIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[writeIndex].dstSet = perFrameDescriptorSets[i];
				writes[writeIndex].dstBinding = writeIndex;
				writes[writeIndex].descriptorCount = 1;
			}

			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[0].pBufferInfo = &uboInfo;

			for (uint32_t storageWrite = 1; storageWrite < static_cast<uint32_t>(writes.size()); ++storageWrite)
			{
				writes[storageWrite].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			}

			writes[1].pBufferInfo = &instanceInfo;
			writes[2].pBufferInfo = &uiInfo;
			writes[3].pBufferInfo = &msdfInfo;
			writes[4].pBufferInfo = &worldInstanceInfo;
			writes[5].pBufferInfo = &gpuWorldStaticInfo;
			writes[6].pBufferInfo = &gpuWorldTransformInfo;
			writes[7].pBufferInfo = &gpuWorldVisibleIndexInfo;

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
			write.dstSet = perFrameDescriptorSets[i];
			write.dstBinding = 1;
			write.dstArrayElement = 0;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write.descriptorCount = 1;
			write.pBufferInfo = &bufferInfo;

			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}
	}

	void VulkanDescriptorManager::CreateWorldInstanceBufferDescriptorSets(const std::vector<std::unique_ptr<VulkanBuffer>>& perFrameWorldInstanceBuffers)
	{
		const uint32_t frameCount = static_cast<uint32_t>(perFrameWorldInstanceBuffers.size());

		for (uint32_t i = 0; i < frameCount; ++i)
		{
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = perFrameWorldInstanceBuffers[i]->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = perFrameDescriptorSets[i];
			write.dstBinding = 4;
			write.dstArrayElement = 0;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write.descriptorCount = 1;
			write.pBufferInfo = &bufferInfo;

			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}
	}

	void VulkanDescriptorManager::CreateGpuWorldDescriptorSets(
		const VulkanBuffer& staticBuffer,
		const std::vector<std::unique_ptr<VulkanBuffer>>& perFrameTransformBuffers,
		const std::vector<std::unique_ptr<VulkanBuffer>>& perFrameVisibleIndexBuffers
	)
	{
		const uint32_t frameCount = static_cast<uint32_t>(perFrameTransformBuffers.size());
		for (uint32_t i = 0; i < frameCount; ++i)
		{
			VkDescriptorBufferInfo staticInfo{};
			staticInfo.buffer = staticBuffer.GetBuffer();
			staticInfo.offset = 0;
			staticInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo transformInfo{};
			transformInfo.buffer = perFrameTransformBuffers[i]->GetBuffer();
			transformInfo.offset = 0;
			transformInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo visibleIndexInfo{};
			visibleIndexInfo.buffer = perFrameVisibleIndexBuffers[i]->GetBuffer();
			visibleIndexInfo.offset = 0;
			visibleIndexInfo.range = VK_WHOLE_SIZE;

			std::array<VkWriteDescriptorSet, 3> writes{};
			for (uint32_t writeIndex = 0; writeIndex < static_cast<uint32_t>(writes.size()); ++writeIndex)
			{
				writes[writeIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[writeIndex].dstSet = perFrameDescriptorSets[i];
				writes[writeIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				writes[writeIndex].descriptorCount = 1;
			}

			writes[0].dstBinding = 5;
			writes[0].pBufferInfo = &staticInfo;
			writes[1].dstBinding = 6;
			writes[1].pBufferInfo = &transformInfo;
			writes[2].dstBinding = 7;
			writes[2].pBufferInfo = &visibleIndexInfo;

			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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
			throw std::runtime_error("Invalid frame index for UIParam SSBO update");
		}

		perFrameMeshDecoratorBuffers[frameIndex]->CopyData(data, size);
	}

	void VulkanDescriptorManager::UpdatePerFrameMsdfBuffer(uint32_t frameIndex, const void* data, size_t size)
	{
		if (frameIndex >= perFrameMsdfBuffers.size())
		{
			throw std::runtime_error("Invalid frame index for UIParam SSBO update");
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

	void VulkanDescriptorManager::CreateBindlessLayout()
	{
		VkDescriptorSetLayoutBinding samplerBinding{};
		samplerBinding.binding = 0;
		samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		samplerBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding textureArrayBinding{};
		textureArrayBinding.binding = 1;
		textureArrayBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		textureArrayBinding.descriptorCount = maxBindlessTextures;
		textureArrayBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		textureArrayBinding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { samplerBinding, textureArrayBinding };

		std::array<VkDescriptorBindingFlags, 2> bindingFlags{};
		bindingFlags[0] = 0;
		bindingFlags[1] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
		extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		extendedInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
		extendedInfo.pBindingFlags = bindingFlags.data();

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.pNext = &extendedInfo;
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &bindlessSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create bindless descriptor set layout!");
		}
	}

	void VulkanDescriptorManager::CreateBindlessPool()
	{
		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		poolSizes[1].descriptorCount = maxBindlessTextures;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
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
		uint32_t variableCount = maxBindlessTextures;
		VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
		countInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
		countInfo.descriptorSetCount = 1;
		countInfo.pDescriptorCounts = &variableCount;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = &countInfo;
		allocInfo.descriptorPool = bindlessDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &bindlessSetLayout;

		if (vkAllocateDescriptorSets(device, &allocInfo, &bindlessDescriptorSet) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate bindless descriptor set!");
		}
	}

	void VulkanDescriptorManager::UpdateBindlessTexture(uint32_t index, VkImageView imageView, VkSampler sampler) const
	{
		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = imageView;
		imageInfo.sampler = sampler;

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
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &samplerInfo;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	VulkanBuffer* VulkanDescriptorManager::GetPerFrameUBO(uint32_t frameIndex) const
	{
		return perFrameUBOs.at(frameIndex).get();
	}

	VulkanBuffer* VulkanDescriptorManager::GetInstanceBufferForFrame(uint32_t frameIndex) const
	{
		return perFrameInstanceBuffers.at(frameIndex).get();
	}

	void VulkanDescriptorManager::EnsurePerFrameBufferCapacity(size_t bytes, std::vector<std::unique_ptr<VulkanBuffer>>& buffers)
	{
		for (auto& buffer : buffers)
		{
			if (!buffer || buffer->GetSize() >= bytes)
			{
				continue;
			}

			auto newBuffer = std::make_unique<VulkanBuffer>(
				device,
				physicalDevice,
				bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			if (buffer)
			{
				buffer->Free();
			}

			buffer = std::move(newBuffer);
		}
	}

	void VulkanDescriptorManager::EnsurePerFrameInstanceCapacity(size_t bytes)
	{
		EnsurePerFrameBufferCapacity(bytes, perFrameInstanceBuffers);
	}

	void VulkanDescriptorManager::EnsurePerFrameMeshDecoratorCapacity(size_t bytes)
	{
		EnsurePerFrameBufferCapacity(bytes, perFrameMeshDecoratorBuffers);
	}

	void VulkanDescriptorManager::EnsurePerFrameMsdfCapacity(size_t bytes)
	{
		EnsurePerFrameBufferCapacity(bytes, perFrameMsdfBuffers);
	}

	void VulkanDescriptorManager::Cleanup()
	{
		for (auto& buffer : perFrameUBOs)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		perFrameUBOs.clear();

		for (auto& buffer : perFrameInstanceBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		perFrameInstanceBuffers.clear();

		for (auto& buffer : perFrameMeshDecoratorBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		perFrameMeshDecoratorBuffers.clear();

		for (auto& buffer : perFrameMsdfBuffers)
		{
			if (buffer)
			{
				buffer->Free();
			}
		}
		perFrameMsdfBuffers.clear();

		perFrameDescriptorSets.clear();

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
	}

}
