#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanStorageTexture.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

#include <algorithm>
#include <bit>

namespace Swim::RhiVulkan
{

	bool ValidateVulkanStorageTexture(const VulkanDeviceState& state, const Rhi::TextureDesc& desc)
	{
		if (!HasTextureUsage(desc.Usage, Rhi::TextureUsage::Storage))
		{
			return true;
		}
		RequireVulkanDevice(state);
		if (!ValidateTextureDesc(desc) || !Rhi::IsStorageTextureFormat(desc.PixelFormat) || desc.Samples != Rhi::SampleCount::X1 ||
			desc.Dimension != Rhi::TextureDimension::Texture2D ||
			desc.MipLevels > static_cast<std::uint32_t>(std::bit_width(std::max(desc.Extent.Width, desc.Extent.Height))))
		{
			return false;
		}
		const auto format = ToVkFormat(desc.PixelFormat);
		VkFormatProperties properties{};
		state.Instance->Dispatch.vkGetPhysicalDeviceFormatProperties(state.Device.physical_device.physical_device, format, &properties);
		if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
		{
			return false;
		}
		// Query the complete usage combination before VMA allocation, including
		// native extent/mip/layer limits; storage support alone is insufficient.
		VkImageFormatProperties image{};
		const auto result = state.Instance->Dispatch.vkGetPhysicalDeviceImageFormatProperties(state.Device.physical_device.physical_device,
			format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, ToVkImageUsage(desc.Usage), 0, &image);
		if (CheckVulkanResult(state, result, "vkGetPhysicalDeviceImageFormatProperties (storage texture)") != VK_SUCCESS)
		{
			return false;
		}
		return desc.Extent.Width <= image.maxExtent.width && desc.Extent.Height <= image.maxExtent.height &&
			desc.Extent.Depth <= image.maxExtent.depth && desc.MipLevels <= image.maxMipLevels &&
			desc.ArrayLayers <= image.maxArrayLayers && (image.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0;
	}

} // namespace Swim::RhiVulkan
