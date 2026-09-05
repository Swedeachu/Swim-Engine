#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTransferUtils.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	std::uint32_t GetColorTexelBytes(Rhi::Format format)
	{
		using Rhi::Format;
		switch (format)
		{
		case Format::R8Unorm: case Format::R8Snorm: case Format::R8Uint: case Format::R8Sint:
			return 1;
		case Format::R16Unorm: case Format::R16Snorm: case Format::R16Uint: case Format::R16Sint: case Format::R16Float:
		case Format::RG8Unorm: case Format::RG8Snorm: case Format::RG8Uint: case Format::RG8Sint:
			return 2;
		case Format::R32Uint: case Format::R32Sint: case Format::R32Float:
		case Format::RG16Unorm: case Format::RG16Snorm: case Format::RG16Uint: case Format::RG16Sint: case Format::RG16Float:
		case Format::RGBA8Unorm: case Format::RGBA8UnormSrgb: case Format::RGBA8Snorm: case Format::RGBA8Uint: case Format::RGBA8Sint:
		case Format::BGRA8Unorm: case Format::BGRA8UnormSrgb:
		case Format::RGB10A2Unorm: case Format::RGB10A2Uint: case Format::R11G11B10Float: case Format::RGB9E5Float:
			return 4;
		case Format::RG32Uint: case Format::RG32Sint: case Format::RG32Float:
		case Format::RGBA16Unorm: case Format::RGBA16Snorm: case Format::RGBA16Uint: case Format::RGBA16Sint: case Format::RGBA16Float:
			return 8;
		case Format::RGB32Uint: case Format::RGB32Sint: case Format::RGB32Float:
			return 12;
		case Format::RGBA32Uint: case Format::RGBA32Sint: case Format::RGBA32Float:
			return 16;
		default:
			throw std::invalid_argument("This Vulkan transfer path requires an uncompressed color format");
		}
	}

	bool IsIntegerColorFormat(Rhi::Format format)
	{
		using Rhi::Format;
		switch (format)
		{
		case Format::R8Uint: case Format::R8Sint: case Format::R16Uint: case Format::R16Sint: case Format::R32Uint: case Format::R32Sint:
		case Format::RG8Uint: case Format::RG8Sint: case Format::RG16Uint: case Format::RG16Sint: case Format::RG32Uint: case Format::RG32Sint:
		case Format::RGB32Uint: case Format::RGB32Sint: case Format::RGBA8Uint: case Format::RGBA8Sint:
		case Format::RGBA16Uint: case Format::RGBA16Sint: case Format::RGBA32Uint: case Format::RGBA32Sint: case Format::RGB10A2Uint:
			return true;
		default:
			return false;
		}
	}

	VkImageSubresourceRange GetSubresourceRange(const Rhi::TextureDesc& desc, const Rhi::TextureSubresourceRange& range)
	{
		if (range.BaseMipLevel >= desc.MipLevels || range.BaseArrayLayer >= desc.ArrayLayers)
		{
			throw std::invalid_argument("Vulkan texture subresource base is out of bounds");
		}
		const std::uint32_t levels = range.MipLevelCount == UINT32_MAX ? desc.MipLevels - range.BaseMipLevel : range.MipLevelCount;
		const std::uint32_t layers = range.ArrayLayerCount == UINT32_MAX ? desc.ArrayLayers - range.BaseArrayLayer : range.ArrayLayerCount;
		if (levels == 0 || layers == 0 || levels > desc.MipLevels - range.BaseMipLevel || layers > desc.ArrayLayers - range.BaseArrayLayer)
		{
			throw std::invalid_argument("Vulkan texture subresource range is empty or out of bounds");
		}
		return { GetImageAspectMask(desc.PixelFormat), range.BaseMipLevel, levels, range.BaseArrayLayer, layers };
	}

	void ValidateCopyExtent(const Rhi::TextureDesc& desc, const Rhi::TextureSubresource& subresource,
		const Rhi::Offset3D& offset, const Rhi::Extent3D& extent)
	{
		if (subresource.MipLevel >= desc.MipLevels || subresource.MipLevel >= 32 || subresource.ArrayLayer >= desc.ArrayLayers)
		{
			throw std::invalid_argument("Vulkan copy subresource is out of bounds");
		}
		const auto fits = [mip = subresource.MipLevel](std::int32_t start, std::uint32_t count, std::uint32_t size)
		{
			const std::uint32_t limit = std::max(1u, size >> mip);
			return start >= 0 && count != 0 && static_cast<std::uint32_t>(start) <= limit && count <= limit - static_cast<std::uint32_t>(start);
		};
		if (!fits(offset.X, extent.Width, desc.Extent.Width) || !fits(offset.Y, extent.Height, desc.Extent.Height) ||
			!fits(offset.Z, extent.Depth, desc.Extent.Depth))
		{
			throw std::invalid_argument("Vulkan copy extent is empty or out of bounds");
		}
	}

	VkBufferImageCopy GetBufferImageCopy(const Rhi::BufferDesc& buffer, const Rhi::TextureDesc& texture,
		const Rhi::BufferTextureCopyRegion& region)
	{
		ValidateCopyExtent(texture, region.Subresource, region.TextureOffset, region.Extent);
		const std::uint32_t texelBytes = GetColorTexelBytes(texture.PixelFormat);
		if (texture.Samples != Rhi::SampleCount::X1 || region.BufferOffset % texelBytes != 0 || region.BufferOffset % 4 != 0)
		{
			throw std::invalid_argument("Vulkan buffer/image copies require single-sample textures and aligned offsets");
		}
		std::uint64_t bytes = texelBytes;
		for (std::uint32_t size : { region.Extent.Width, region.Extent.Height, region.Extent.Depth })
		{
			if (bytes > std::numeric_limits<std::uint64_t>::max() / size)
			{
				throw std::invalid_argument("Vulkan buffer/image copy byte count overflow");
			}
			bytes *= size;
		}
		if (region.BufferOffset > buffer.Size || bytes > buffer.Size - region.BufferOffset)
		{
			throw std::invalid_argument("Vulkan buffer/image copy exceeds the buffer range");
		}
		VkBufferImageCopy result{};
		result.bufferOffset = region.BufferOffset;
		result.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, region.Subresource.MipLevel, region.Subresource.ArrayLayer, 1 };
		result.imageOffset = { region.TextureOffset.X, region.TextureOffset.Y, region.TextureOffset.Z };
		result.imageExtent = { region.Extent.Width, region.Extent.Height, region.Extent.Depth };
		return result;
	}

} // namespace Swim::RhiVulkan
