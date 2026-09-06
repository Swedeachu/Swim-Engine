#pragma once

#include "Engine/Systems/Renderer/RHI/RhiPipelineCache.h"

#include <volk.h>

#include <span>

namespace Swim::RhiVulkan
{

	inline constexpr std::size_t VulkanPipelineCacheEnvelopeBytes = 56;

	bool IsCompatibleVulkanPipelineCacheHeader(std::span<const std::byte> data, const VkPhysicalDeviceProperties& properties);
	Rhi::PipelineCacheLoadStatus DecodeVulkanPipelineCacheData(std::span<const std::byte> data,
		const VkPhysicalDeviceProperties& properties, std::span<const std::byte>& nativeData);
	std::vector<std::byte> EncodeVulkanPipelineCacheData(std::span<const std::byte> nativeData,
		const VkPhysicalDeviceProperties& properties);

} // namespace Swim::RhiVulkan
