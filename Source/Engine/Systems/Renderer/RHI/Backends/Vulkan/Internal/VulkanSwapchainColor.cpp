#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanSwapchainColor.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDeviceState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace Swim::RhiVulkan
{
	namespace
	{
		using Rhi::Format;
		using Rhi::SwapchainColorSpace;
		// Order is deterministic, independent of driver enumeration order.
		constexpr std::array SupportedPairs{
			Rhi::SwapchainSurfaceFormat{ Format::RGB10A2Unorm, SwapchainColorSpace::Hdr10St2084 },
			Rhi::SwapchainSurfaceFormat{ Format::BGR10A2Unorm, SwapchainColorSpace::Hdr10St2084 },
			Rhi::SwapchainSurfaceFormat{ Format::RGBA16Float, SwapchainColorSpace::ExtendedSrgbLinear },
			Rhi::SwapchainSurfaceFormat{ Format::BGRA8UnormSrgb, SwapchainColorSpace::SrgbNonlinear },
			Rhi::SwapchainSurfaceFormat{ Format::RGBA8UnormSrgb, SwapchainColorSpace::SrgbNonlinear },
			Rhi::SwapchainSurfaceFormat{ Format::BGRA8Unorm, SwapchainColorSpace::SrgbNonlinear },
			Rhi::SwapchainSurfaceFormat{ Format::RGBA8Unorm, SwapchainColorSpace::SrgbNonlinear },
		};

		void RequireQuerySuccess(const VulkanDeviceState& state, VkResult result, const char* operation)
		{
			CheckVulkanResult(state, result, operation);
			if (result != VK_SUCCESS)
			{
				throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
			}
		}
	}

	VkSurfaceFormatKHR ToVkSurfaceFormat(const Rhi::SwapchainSurfaceFormat& format)
	{
		VkColorSpaceKHR colorSpace;
		switch (format.ColorSpace)
		{
		case SwapchainColorSpace::SrgbNonlinear:
			colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			break;
		case SwapchainColorSpace::Hdr10St2084:
			colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT;
			break;
		case SwapchainColorSpace::ExtendedSrgbLinear:
			colorSpace = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
			break;
		default:
			throw std::invalid_argument("Undefined swapchain color space");
		}
		return { ToVkFormat(format.PixelFormat), colorSpace };
	}

	bool MatchesSwapchainFormat(const Rhi::SwapchainSurfaceFormat& selected, VkFormat format, VkColorSpaceKHR colorSpace)
	{
		const auto native = ToVkSurfaceFormat(selected);
		return native.format == format && native.colorSpace == colorSpace;
	}

	Rhi::SwapchainSupport BuildSwapchainSupport(std::span<const VkSurfaceFormatKHR> formats, bool colorSpaceEnabled)
	{
		Rhi::SwapchainSupport support{};
		support.PresentationSupported = true;
		for (const auto& pair : SupportedPairs)
		{
			if (pair.IsHdr() && !colorSpaceEnabled)
			{
				continue;
			}
			for (const auto& native : formats)
			{
				if (MatchesSwapchainFormat(pair, native.format, native.colorSpace))
				{
					support.Formats.push_back(pair);
					break;
				}
			}
		}
		return support;
	}

	Rhi::SwapchainSupport QueryVulkanSwapchainSupport(const VulkanDeviceState& state, VkSurfaceKHR surface)
	{
		RequireVulkanDevice(state);
		const auto& dispatch = state.Instance->Dispatch;
		const auto physicalDevice = state.Device.physical_device.physical_device;
		VkBool32 supported = VK_FALSE;
		RequireQuerySuccess(state, dispatch.vkGetPhysicalDeviceSurfaceSupportKHR(
			physicalDevice, state.QueueFamilies.Graphics, surface, &supported), "vkGetPhysicalDeviceSurfaceSupportKHR");
		if (!supported)
		{
			return {};
		}
		// Re-enumerate on VK_INCOMPLETE; never select from a truncated snapshot.
		// Bound both allocation and retries for a changing/broken surface driver.
		for (unsigned attempt = 0; attempt < 4; ++attempt)
		{
			std::uint32_t count = 0;
			const auto countResult = dispatch.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
			if (countResult == VK_INCOMPLETE)
			{
				continue;
			}
			RequireQuerySuccess(state, countResult, "vkGetPhysicalDeviceSurfaceFormatsKHR (count)");
			if (count > 4096)
			{
				throw std::runtime_error("Unreasonable Vulkan surface format count");
			}
			if (count == 0)
			{
				return BuildSwapchainSupport({}, state.Instance->SwapchainColorSpaceEnabled);
			}
			std::vector<VkSurfaceFormatKHR> formats(count);
			const auto result = dispatch.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
			if (result == VK_INCOMPLETE)
			{
				continue;
			}
			RequireQuerySuccess(state, result, "vkGetPhysicalDeviceSurfaceFormatsKHR (data)");
			if (count > formats.size())
			{
				throw std::runtime_error("Vulkan surface format count exceeded capacity");
			}
			formats.resize(count);
			return BuildSwapchainSupport(formats, state.Instance->SwapchainColorSpaceEnabled);
		}
		throw std::runtime_error("Vulkan surface format enumeration did not stabilize");
	}

	std::optional<Rhi::SwapchainSurfaceFormat> SelectSwapchainFormat(
		const Rhi::SwapchainSupport& support, const Rhi::SwapchainDesc& desc)
	{
		if (desc.ColorMode != Rhi::SwapchainColorMode::Sdr && desc.ColorMode != Rhi::SwapchainColorMode::PreferHdr &&
			desc.ColorMode != Rhi::SwapchainColorMode::RequireHdr)
		{
			throw std::invalid_argument("Invalid swapchain color mode");
		}
		if (!support.PresentationSupported)
		{
			return {};
		}
		const bool hdr = desc.ColorMode != Rhi::SwapchainColorMode::Sdr && support.SupportsHdr();
		if (!hdr && desc.ColorMode == Rhi::SwapchainColorMode::RequireHdr)
		{
			return {};
		}
		std::optional<Rhi::SwapchainSurfaceFormat> fallback;
		for (const auto& pair : support.Formats)
		{
			if (pair.IsHdr() != hdr)
			{
				continue;
			}
			if (pair.PixelFormat == desc.PreferredFormat)
			{
				return pair;
			}
			if (!fallback)
			{
				fallback = pair;
			}
		}
		return fallback;
	}

} // namespace Swim::RhiVulkan
