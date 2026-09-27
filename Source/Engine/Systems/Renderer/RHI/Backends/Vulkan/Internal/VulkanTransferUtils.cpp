#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTransferUtils.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/RhiFormatInfo.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	std::uint32_t GetColorTexelBytes(Rhi::Format format)
	{
		const std::uint32_t bytes = Rhi::GetUncompressedColorTexelBytes(format);
		if (bytes == 0)
		{
			throw std::invalid_argument("This Vulkan transfer path requires an uncompressed color format");
		}
		return bytes;
	}

	std::uint32_t RequireTransferTexelBytes(Rhi::Format format)
	{
		const std::uint32_t bytes = Rhi::GetTransferTexelBytes(format);
		if (bytes == 0)
		{
			throw std::invalid_argument("Vulkan buffer/image copies require an uncompressed color format or D32Float");
		}
		return bytes;
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
		const auto block = Rhi::GetTransferBlockInfo(texture.PixelFormat);
		if (block.Bytes == 0)
		{
			throw std::invalid_argument("Vulkan buffer/image copies require an uncompressed color, BC or D32Float format");
		}
		if (texture.Samples != Rhi::SampleCount::X1 || region.BufferOffset % block.Bytes != 0 || region.BufferOffset % 4 != 0)
		{
			throw std::invalid_argument("Vulkan buffer/image copies require single-sample textures and aligned offsets");
		}
		if (block.Width > 1)
		{
			// Block formats copy whole blocks: offsets on block corners, extents a block
			// multiple unless they end at the mip's edge.
			const std::uint32_t mip = region.Subresource.MipLevel;
			const std::uint32_t mipWidth = std::max(1u, texture.Extent.Width >> mip);
			const std::uint32_t mipHeight = std::max(1u, texture.Extent.Height >> mip);
			const auto aligned = [](std::int32_t start, std::uint32_t count, std::uint32_t limit, std::uint32_t unit)
			{ return start % std::int32_t(unit) == 0 && (count % unit == 0 || std::uint32_t(start) + count == limit); };
			if (!aligned(region.TextureOffset.X, region.Extent.Width, mipWidth, block.Width) ||
				!aligned(region.TextureOffset.Y, region.Extent.Height, mipHeight, block.Height))
			{
				throw std::invalid_argument("Vulkan block-compressed copies must cover whole blocks");
			}
		}
		const std::uint64_t columns = (std::uint64_t(region.Extent.Width) + block.Width - 1) / block.Width;
		const std::uint64_t rows = (std::uint64_t(region.Extent.Height) + block.Height - 1) / block.Height;
		std::uint64_t bytes = block.Bytes;
		for (std::uint64_t size : { columns, rows, std::uint64_t(region.Extent.Depth) })
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
		// D32Float copies its depth aspect; every other accepted format is color.
		const VkImageAspectFlags aspect =
			texture.PixelFormat == Rhi::Format::D32Float ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		result.imageSubresource = { aspect, region.Subresource.MipLevel, region.Subresource.ArrayLayer, 1 };
		result.imageOffset = { region.TextureOffset.X, region.TextureOffset.Y, region.TextureOffset.Z };
		result.imageExtent = { region.Extent.Width, region.Extent.Height, region.Extent.Depth };
		return result;
	}

} // namespace Swim::RhiVulkan
