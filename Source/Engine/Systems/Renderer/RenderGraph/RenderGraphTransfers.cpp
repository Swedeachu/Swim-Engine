#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RHI/RhiFormatInfo.h"
#include <algorithm>
#include <bit>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;

		std::string StagingName(std::string_view pass, std::string_view suffix)
		{
			std::string name(pass);
			name += suffix;
			return name;
		}

		void RequireBufferRange(const Rhi::BufferDesc& desc, std::uint64_t offset, std::uint64_t size, const char* what)
		{
			if (!size || offset > desc.Size || size > desc.Size - offset)
			{
				throw std::invalid_argument(std::string("RenderGraph ") + what + " needs a nonempty in-bounds buffer range");
			}
		}

		std::uint32_t MipExtent(std::uint32_t extent, std::uint32_t mip)
		{
			return std::max(1u, mip < 32 ? extent >> mip : 0u);
		}

		// Validates the region and reports whether it covers its whole subresource.
		bool ValidateTextureRegion(const Rhi::TextureDesc& desc, const Rhi::BufferTextureCopyRegion& region)
		{
			const auto& sub = region.Subresource;
			if (sub.MipLevel >= desc.MipLevels || sub.ArrayLayer >= desc.ArrayLayers || desc.Samples != Rhi::SampleCount::X1)
			{
				throw std::invalid_argument("RenderGraph texture transfer needs a single-sample subresource in range");
			}
			const std::uint32_t extent[3] = { MipExtent(desc.Extent.Width, sub.MipLevel), MipExtent(desc.Extent.Height, sub.MipLevel),
				MipExtent(desc.Extent.Depth, sub.MipLevel) };
			const std::int32_t offset[3] = { region.TextureOffset.X, region.TextureOffset.Y, region.TextureOffset.Z };
			const std::uint32_t size[3] = { region.Extent.Width, region.Extent.Height, region.Extent.Depth };
			bool whole = true;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (offset[axis] < 0 || !size[axis] || std::uint64_t(offset[axis]) + size[axis] > extent[axis])
				{
					throw std::invalid_argument("RenderGraph texture transfer region exceeds its subresource");
				}
				whole = whole && offset[axis] == 0 && size[axis] == extent[axis];
			}
			return whole;
		}

		std::uint64_t TexelAlignment(const Rhi::TextureDesc& desc)
		{
			const auto texel = Rhi::GetUncompressedColorTexelBytes(desc.PixelFormat);
			if (!texel || !std::has_single_bit(texel))
			{
				throw std::invalid_argument("RenderGraph texture transfers need an uncompressed power-of-two-texel color format");
			}
			return std::max<std::uint64_t>(4, texel);
		}
	} // namespace

	std::uint64_t GetTextureCopyBytes(const Rhi::TextureDesc& texture, const Rhi::BufferTextureCopyRegion& region)
	{
		std::uint64_t bytes = Rhi::GetUncompressedColorTexelBytes(texture.PixelFormat);
		if (!bytes)
		{
			throw std::invalid_argument("RenderGraph texture transfers need an uncompressed color format");
		}
		for (std::uint32_t size : { region.Extent.Width, region.Extent.Height, region.Extent.Depth })
		{
			if (!size || bytes > UINT64_MAX / size)
			{
				throw std::invalid_argument("RenderGraph texture transfer byte count is empty or overflows");
			}
			bytes *= size;
		}
		return bytes;
	}

	GraphPass AddBufferUpload(RenderGraph& graph, std::string_view name, std::uint64_t size, GraphUploadWriter writer,
		GraphBuffer destination, std::uint64_t destinationOffset)
	{
		const auto& desc = graph.GetDesc(destination);
		RequireBufferRange(desc, destinationOffset, size, "buffer upload");
		const bool whole = destinationOffset == 0 && size == desc.Size;
		const auto stagingName = StagingName(name, " staging");
		const auto staging = graph.CreateUpload({ size, Rhi::BufferUsage::TransferSource, 4, stagingName }, std::move(writer));
		return graph.AddPass(
			name, Rhi::QueueType::Transfer,
			[&](RenderGraphBuilder& b)
			{
				b.Read(staging, S::CopySource);
				if (whole)
				{
					b.Write(destination, S::CopyDestination);
				}
				else
				{
					b.ReadWrite(destination, S::CopyDestination);
				}
			},
			[=](RenderCommandContext& c)
			{
				const auto source = c.GetRange(staging);
				const auto target = c.GetRange(destination);
				c.Commands().CopyBuffer(*source.Buffer, *target.Buffer, { source.Offset, target.Offset + destinationOffset, size });
			});
	}

	GraphPass AddBufferUpload(RenderGraph& graph, std::string_view name, std::span<const std::byte> data, GraphBuffer destination,
		std::uint64_t destinationOffset)
	{
		auto owned = std::make_shared<const std::vector<std::byte>>(data.begin(), data.end());
		return AddBufferUpload(
			graph, name, data.size(),
			[owned](std::span<std::byte> bytes)
			{
				std::copy(owned->begin(), owned->end(), bytes.begin());
			},
			destination, destinationOffset);
	}

	GraphPass AddTextureUpload(RenderGraph& graph, std::string_view name, std::span<const std::byte> data, GraphTexture destination,
		const Rhi::BufferTextureCopyRegion& region)
	{
		const auto& desc = graph.GetDesc(destination);
		const bool whole = ValidateTextureRegion(desc, region);
		const auto alignment = TexelAlignment(desc);
		if (data.size() != GetTextureCopyBytes(desc, region))
		{
			throw std::invalid_argument("RenderGraph texture upload data must be tightly packed for its region");
		}
		const auto staging = graph.CreateUpload(data, StagingName(name, " staging"), Rhi::BufferUsage::TransferSource, alignment);
		const Rhi::TextureSubresourceRange range{ region.Subresource.MipLevel, 1, region.Subresource.ArrayLayer, 1 };
		return graph.AddPass(
			name, Rhi::QueueType::Transfer,
			[&](RenderGraphBuilder& b)
			{
				b.Read(staging, S::CopySource);
				if (whole)
				{
					b.Write(destination, S::CopyDestination, range);
				}
				else
				{
					b.ReadWrite(destination, S::CopyDestination, range);
				}
			},
			[=](RenderCommandContext& c)
			{
				const auto source = c.GetRange(staging);
				auto copy = region;
				copy.BufferOffset = source.Offset;
				c.Commands().CopyBufferToTexture(*source.Buffer, c.Get(destination, range), copy);
			});
	}

	GraphReadback AddBufferReadback(
		RenderGraph& graph, std::string_view name, GraphBuffer source, std::uint64_t sourceOffset, std::uint64_t size)
	{
		RequireBufferRange(graph.GetDesc(source), sourceOffset, size, "buffer readback");
		GraphReadback result;
		result.Buffer = graph.CreateReadback({ size, 4, StagingName(name, " readback") });
		const auto readback = result.Buffer;
		result.Pass = graph.AddPass(
			name, Rhi::QueueType::Transfer,
			[&](RenderGraphBuilder& b)
			{
				b.Read(source, S::CopySource);
				b.Write(readback, S::CopyDestination);
			},
			[=](RenderCommandContext& c)
			{
				const auto from = c.GetRange(source);
				const auto to = c.GetRange(readback);
				c.Commands().CopyBuffer(*from.Buffer, *to.Buffer, { from.Offset + sourceOffset, to.Offset, size });
			});
		return result;
	}

	GraphReadback AddTextureReadback(
		RenderGraph& graph, std::string_view name, GraphTexture source, const Rhi::BufferTextureCopyRegion& region)
	{
		const auto& desc = graph.GetDesc(source);
		ValidateTextureRegion(desc, region);
		const auto alignment = TexelAlignment(desc);
		GraphReadback result;
		result.Buffer = graph.CreateReadback({ GetTextureCopyBytes(desc, region), alignment, StagingName(name, " readback") });
		const auto readback = result.Buffer;
		const Rhi::TextureSubresourceRange range{ region.Subresource.MipLevel, 1, region.Subresource.ArrayLayer, 1 };
		result.Pass = graph.AddPass(
			name, Rhi::QueueType::Transfer,
			[&](RenderGraphBuilder& b)
			{
				b.Read(source, S::CopySource, range);
				b.Write(readback, S::CopyDestination);
			},
			[=](RenderCommandContext& c)
			{
				const auto to = c.GetRange(readback);
				auto copy = region;
				copy.BufferOffset = to.Offset;
				c.Commands().CopyTextureToBuffer(c.Get(source, range), *to.Buffer, copy);
			});
		return result;
	}
} // namespace Swim::Render
