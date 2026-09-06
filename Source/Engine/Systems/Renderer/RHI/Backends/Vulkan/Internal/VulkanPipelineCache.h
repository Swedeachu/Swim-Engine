#pragma once

#include "Engine/Systems/Renderer/RHI/RhiPipelineCache.h"

#include <volk.h>

#include <shared_mutex>
#include <span>

namespace Swim::RhiVulkan
{

	struct VulkanDeviceState;

	struct VulkanPipelineCacheState
	{
		mutable std::shared_mutex Mutex;
		VkPipelineCache Handle = VK_NULL_HANDLE;
		bool InitializationAttempted = false;
	};

	Rhi::PipelineCacheLoadStatus LoadVulkanPipelineCache(const VulkanDeviceState& state, std::span<const std::byte> data);
	Rhi::PipelineCacheData ExportVulkanPipelineCache(const VulkanDeviceState& state);
	VkResult CreateCachedVulkanGraphicsPipeline(const VulkanDeviceState& state, const VkGraphicsPipelineCreateInfo& info, VkPipeline& pipeline);
	void DestroyVulkanPipelineCache(const VulkanDeviceState& state) noexcept;

} // namespace Swim::RhiVulkan
