#include "Tests/Fixtures/VulkanDescriptorCapture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"

namespace Swim::Testing
{

	namespace
	{
		VulkanDescriptorCapture* capture = nullptr;
	}

	VulkanDescriptorCapture::VulkanDescriptorCapture()
	{
		capture = this;
		auto& limits = State->Device.physical_device.properties.limits;
		limits.maxBoundDescriptorSets = 8;
		limits.maxDescriptorSetSamplers = limits.maxDescriptorSetSampledImages = 128;
		limits.maxDescriptorSetUniformBuffers = limits.maxDescriptorSetStorageBuffers = 64;
		limits.maxPerStageDescriptorSamplers = limits.maxPerStageDescriptorSampledImages = 64;
		limits.maxPerStageDescriptorUniformBuffers = limits.maxPerStageDescriptorStorageBuffers = 32;
		limits.maxPerStageResources = 128;
		limits.minUniformBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment = 16;
		limits.maxUniformBufferRange = 1024;
		limits.maxStorageBufferRange = 4096;
		limits.maxSamplerAllocationCount = 64;
		limits.maxSamplerLodBias = 4;
		FormatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
		State->Dispatch.vkGetDescriptorSetLayoutSupport = +[](VkDevice, const VkDescriptorSetLayoutCreateInfo*, VkDescriptorSetLayoutSupport* support)
		{
			support->supported = capture->LayoutSupported;
		};
		State->Dispatch.vkCreateDescriptorSetLayout = +[](VkDevice, const VkDescriptorSetLayoutCreateInfo* info,
			const VkAllocationCallbacks*, VkDescriptorSetLayout* set) -> VkResult
		{
			capture->SetBindings.emplace_back();
			for (std::uint32_t index = 0; index < info->bindingCount; ++index)
			{
				capture->SetBindings.back().push_back(info->pBindings[index]);
			}
			if (capture->SetBindings.size() == capture->FailSet)
			{
				return VK_ERROR_OUT_OF_HOST_MEMORY;
			}
			*set = RhiVulkan::FromNativeHandle<VkDescriptorSetLayout>(capture->SetBindings.size());
			return VK_SUCCESS;
		};
		State->Dispatch.vkDestroyDescriptorSetLayout = +[](VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) { ++capture->SetsDestroyed; };
		State->Dispatch.vkCreateDescriptorPool = +[](VkDevice, const VkDescriptorPoolCreateInfo* info,
			const VkAllocationCallbacks*, VkDescriptorPool* pool) -> VkResult
		{
			++capture->PoolsCreated;
			capture->PoolSizes.assign(info->pPoolSizes, info->pPoolSizes + info->poolSizeCount);
			if (capture->PoolResult == VK_SUCCESS)
			{
				*pool = RhiVulkan::FromNativeHandle<VkDescriptorPool>(capture->PoolsCreated);
			}
			return capture->PoolResult;
		};
		State->Dispatch.vkDestroyDescriptorPool = +[](VkDevice, VkDescriptorPool, const VkAllocationCallbacks*) { ++capture->PoolsDestroyed; };
		State->Dispatch.vkAllocateDescriptorSets = +[](VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet* set) -> VkResult
		{
			*set = RhiVulkan::FromNativeHandle<VkDescriptorSet>(1);
			return capture->AllocationResult;
		};
		State->Dispatch.vkUpdateDescriptorSets = +[](VkDevice, std::uint32_t count, const VkWriteDescriptorSet* writes, std::uint32_t, const VkCopyDescriptorSet*)
		{
			++capture->Updates;
			capture->Writes.assign(writes, writes + count);
			capture->ImagesWritten.clear();
			capture->BuffersWritten.clear();
			for (std::uint32_t index = 0; index < count; ++index)
			{
				if (writes[index].pImageInfo)
				{
					capture->ImagesWritten.push_back(*writes[index].pImageInfo);
				}
				if (writes[index].pBufferInfo)
				{
					capture->BuffersWritten.push_back(*writes[index].pBufferInfo);
				}
				capture->Writes[index].pImageInfo = nullptr;
				capture->Writes[index].pBufferInfo = nullptr;
			}
		};
		State->Dispatch.vkCreateSampler = +[](VkDevice, const VkSamplerCreateInfo* info, const VkAllocationCallbacks*, VkSampler* sampler) -> VkResult
		{
			++capture->SamplersCreated;
			capture->SamplerInfo = *info;
			if (capture->SamplerResult == VK_SUCCESS)
			{
				*sampler = RhiVulkan::FromNativeHandle<VkSampler>(capture->SamplersCreated);
			}
			return capture->SamplerResult;
		};
		State->Dispatch.vkDestroySampler = +[](VkDevice, VkSampler, const VkAllocationCallbacks*) { ++capture->SamplersDestroyed; };
		State->Dispatch.vkCmdBindDescriptorSets = +[](VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,
			std::uint32_t space, std::uint32_t, const VkDescriptorSet*, std::uint32_t, const std::uint32_t*)
		{
			++capture->DescriptorBinds;
			capture->BoundSpace = space;
		};
	}

} // namespace Swim::Testing
