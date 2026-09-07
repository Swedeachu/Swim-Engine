#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>
#include <optional>
#include <span>

namespace Swim::RhiVulkan
{
	struct VulkanDeviceState;

	Rhi::SwapchainSupport BuildSwapchainSupport(std::span<const VkSurfaceFormatKHR> formats, bool colorSpaceEnabled);
	Rhi::SwapchainSupport QueryVulkanSwapchainSupport(const VulkanDeviceState& state, VkSurfaceKHR surface);
	std::optional<Rhi::SwapchainSurfaceFormat> SelectSwapchainFormat(
		const Rhi::SwapchainSupport& support, const Rhi::SwapchainDesc& desc);
	VkSurfaceFormatKHR ToVkSurfaceFormat(const Rhi::SwapchainSurfaceFormat& format);
	bool MatchesSwapchainFormat(const Rhi::SwapchainSurfaceFormat& selected, VkFormat format, VkColorSpaceKHR colorSpace);

} // namespace Swim::RhiVulkan
