#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanDepthSampling.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

#include <algorithm>
#include <bit>

namespace Swim::RhiVulkan
{

	bool SupportsVulkanDepthSampling(const VulkanDeviceState& state, Rhi::Format format)
	{
		VkFormatProperties3 features{};
		features.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
		VkFormatProperties2 properties{};
		properties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
		properties.pNext = &features;
		state.Instance->Dispatch.vkGetPhysicalDeviceFormatProperties2(state.Device.physical_device.physical_device,
			ToVkFormat(format), &properties);
		const auto required = VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
		return (features.optimalTilingFeatures & required) == required;
	}

	bool ValidateVulkanSampledDepthTexture(const VulkanDeviceState& state, const Rhi::TextureDesc& desc)
	{
		if (!Rhi::IsDepthFormat(desc.PixelFormat) || !HasTextureUsage(desc.Usage, Rhi::TextureUsage::Sampled))
		{
			return true;
		}
		RequireVulkanDevice(state);
		if (!ValidateTextureDesc(desc) || desc.Samples != Rhi::SampleCount::X1 ||
			(desc.Dimension != Rhi::TextureDimension::Texture2D && desc.Dimension != Rhi::TextureDimension::TextureCube) ||
			desc.MipLevels > static_cast<std::uint32_t>(std::bit_width(std::max(desc.Extent.Width, desc.Extent.Height))) ||
			!SupportsVulkanDepthSampling(state, desc.PixelFormat))
		{
			return false;
		}
		VkImageFormatProperties properties{};
		const VkImageCreateFlags flags = desc.Dimension == Rhi::TextureDimension::TextureCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
		const auto result = state.Instance->Dispatch.vkGetPhysicalDeviceImageFormatProperties(state.Device.physical_device.physical_device,
			ToVkFormat(desc.PixelFormat), VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, ToVkImageUsage(desc.Usage), flags, &properties);
		if (CheckVulkanResult(state, result, "vkGetPhysicalDeviceImageFormatProperties (sampled depth)") != VK_SUCCESS)
		{
			return false;
		}
		return desc.Extent.Width <= properties.maxExtent.width && desc.Extent.Height <= properties.maxExtent.height &&
			desc.Extent.Depth <= properties.maxExtent.depth && desc.MipLevels <= properties.maxMipLevels &&
			desc.ArrayLayers <= properties.maxArrayLayers && (properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0;
	}

} // namespace Swim::RhiVulkan
