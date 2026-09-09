#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTextureViews.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

namespace Swim::RhiVulkan
{

	VkImageAspectFlags GetVulkanTextureViewAspect(Rhi::Format format, Rhi::TextureAspect aspect)
	{
		const auto available = GetImageAspectMask(format);
		switch (aspect)
		{
		case Rhi::TextureAspect::Automatic: return available;
		case Rhi::TextureAspect::Color: return available & VK_IMAGE_ASPECT_COLOR_BIT;
		case Rhi::TextureAspect::Depth: return available & VK_IMAGE_ASPECT_DEPTH_BIT;
		case Rhi::TextureAspect::Stencil: return available & VK_IMAGE_ASPECT_STENCIL_BIT;
		default: return 0;
		}
	}

	bool ValidateVulkanTextureView(const Rhi::TextureDesc& texture, const Rhi::TextureViewDesc& view, bool cubeArrays)
	{
		const auto format = view.PixelFormat == Rhi::Format::Undefined ? texture.PixelFormat : view.PixelFormat;
		if (GetVulkanTextureViewAspect(format, view.Aspect) == 0 || format != texture.PixelFormat || ToVkFormat(format) == VK_FORMAT_UNDEFINED ||
			view.MipLevelCount == 0 || view.ArrayLayerCount == 0 || view.BaseMipLevel >= texture.MipLevels ||
			view.MipLevelCount > texture.MipLevels - view.BaseMipLevel || view.BaseArrayLayer >= texture.ArrayLayers ||
			view.ArrayLayerCount > texture.ArrayLayers - view.BaseArrayLayer)
		{
			return false;
		}
		if (texture.Samples != Rhi::SampleCount::X1 && view.Dimension != Rhi::TextureViewDimension::Texture2D &&
			view.Dimension != Rhi::TextureViewDimension::Texture2DArray)
		{
			return false;
		}
		using Rhi::TextureDimension;
		using Rhi::TextureViewDimension;
		switch (view.Dimension)
		{
		case TextureViewDimension::Texture1D:
			return texture.Dimension == TextureDimension::Texture1D && view.ArrayLayerCount == 1;
		case TextureViewDimension::Texture1DArray:
			return texture.Dimension == TextureDimension::Texture1D;
		case TextureViewDimension::Texture2D:
			return (texture.Dimension == TextureDimension::Texture2D || texture.Dimension == TextureDimension::TextureCube) && view.ArrayLayerCount == 1;
		case TextureViewDimension::Texture2DArray:
			return texture.Dimension == TextureDimension::Texture2D || texture.Dimension == TextureDimension::TextureCube;
		case TextureViewDimension::Texture3D:
			return texture.Dimension == TextureDimension::Texture3D && view.BaseArrayLayer == 0 && view.ArrayLayerCount == 1;
		case TextureViewDimension::TextureCube:
			return texture.Dimension == TextureDimension::TextureCube && view.ArrayLayerCount == 6;
		case TextureViewDimension::TextureCubeArray:
			return cubeArrays && texture.Dimension == TextureDimension::TextureCube && view.ArrayLayerCount % 6 == 0;
		default: return false;
		}
	}

} // namespace Swim::RhiVulkan
