#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTextureViews.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

namespace Swim::RhiVulkan
{

	bool ValidateVulkanTextureView(const Rhi::TextureDesc& texture, const Rhi::TextureViewDesc& view, bool cubeArrays)
	{
		const auto format = view.PixelFormat == Rhi::Format::Undefined ? texture.PixelFormat : view.PixelFormat;
		if (format != texture.PixelFormat || ToVkFormat(format) == VK_FORMAT_UNDEFINED ||
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
