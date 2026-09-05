#pragma once

#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorTable.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanSampler.h"

namespace Swim::Testing
{

	struct VulkanDescriptorCapture : VulkanPipelineCapture
	{
		VulkanDescriptorCapture();
		std::vector<std::vector<VkDescriptorSetLayoutBinding>> SetBindings;
		std::vector<VkDescriptorPoolSize> PoolSizes;
		std::vector<VkDescriptorImageInfo> ImagesWritten;
		std::vector<VkDescriptorBufferInfo> BuffersWritten;
		std::vector<VkWriteDescriptorSet> Writes;
		VkSamplerCreateInfo SamplerInfo{};
		std::uint32_t SetsDestroyed = 0;
		std::uint32_t PoolsCreated = 0;
		std::uint32_t PoolsDestroyed = 0;
		std::uint32_t SamplersCreated = 0;
		std::uint32_t SamplersDestroyed = 0;
		std::uint32_t Updates = 0;
		std::uint32_t DescriptorBinds = 0;
		std::uint32_t BoundSpace = 0;
		std::uint32_t FailSet = 0;
		bool LayoutSupported = true;
		VkResult PoolResult = VK_SUCCESS;
		VkResult AllocationResult = VK_SUCCESS;
		VkResult SamplerResult = VK_SUCCESS;
	};

} // namespace Swim::Testing
