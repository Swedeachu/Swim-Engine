#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::RhiVulkan
{

	std::vector<VkPushConstantRange> BuildVulkanPushConstantRanges(const VulkanDeviceState& state,
		std::span<const Rhi::PushConstantRange> ranges, VkShaderStageFlags programStages);
	bool CompatibleVulkanPushConstants(std::span<const VkPushConstantRange> left, std::span<const VkPushConstantRange> right);
	VkShaderStageFlags ValidateVulkanPushConstantWrite(std::span<const VkPushConstantRange> ranges,
		Rhi::ShaderStageMask stages, std::uint32_t offset, std::size_t size);

} // namespace Swim::RhiVulkan
