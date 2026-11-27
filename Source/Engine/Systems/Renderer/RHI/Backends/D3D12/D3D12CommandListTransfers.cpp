#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12CommandList.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::RhiD3D12
{

	namespace
	{

		// One subresource's copy rows: tight in the RHI buffer, pitch-aligned in a footprint.
		struct CopyRows
		{
			std::uint32_t BlockWidth = 1; // Texels per block edge (4 for block compression).
			std::uint32_t RowBytes = 0;	  // Tight bytes per row of blocks.
			std::uint32_t Rows = 0;		  // Rows of blocks per slice.
			std::uint32_t Depth = 1;
			std::uint32_t Pitch = 0; // Footprint row pitch (256-byte aligned unless unrestricted).
			std::uint64_t Placement = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; // Footprint offset alignment.
		};

		// Without OPTIONS13 UnrestrictedBufferTextureCopyPitchSupported, a placed footprint's row pitch
		// is a multiple of D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256) and its offset of
		// D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT (512) (D3D12_PLACED_SUBRESOURCE_FOOTPRINT,
		// D3D12_SUBRESOURCE_FOOTPRINT). With it, both need only be whole texels (or blocks), so the
		// RHI's tight rows are used in place.
		CopyRows Measure(Rhi::Format format, const Rhi::Extent3D& extent, bool unrestricted)
		{
			CopyRows rows;
			rows.BlockWidth = IsBlockCompressed(format) ? 4u : 1u;
			const auto blocksWide = (extent.Width + rows.BlockWidth - 1) / rows.BlockWidth;
			rows.Rows = (extent.Height + rows.BlockWidth - 1) / rows.BlockWidth;
			rows.RowBytes = blocksWide * FormatBlockBytes(format);
			rows.Depth = extent.Depth;
			rows.Pitch = unrestricted ? rows.RowBytes : AlignUp(rows.RowBytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
			rows.Placement = unrestricted ? FormatBlockBytes(format) : D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
			return rows;
		}

		D3D12_TEXTURE_COPY_LOCATION Footprint(
			ID3D12Resource* buffer, std::uint64_t offset, Rhi::Format format, const Rhi::Extent3D& extent, const CopyRows& rows)
		{
			D3D12_TEXTURE_COPY_LOCATION location{};
			location.pResource = buffer;
			location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			location.PlacedFootprint.Offset = offset;
			location.PlacedFootprint.Footprint = { ToDxgiFormat(format), AlignUp(extent.Width, rows.BlockWidth),
				AlignUp(extent.Height, rows.BlockWidth), extent.Depth, rows.Pitch };
			return location;
		}

		D3D12_TEXTURE_COPY_LOCATION Subresource(const D3D12Texture& texture, const Rhi::TextureSubresource& subresource)
		{
			D3D12_TEXTURE_COPY_LOCATION location{};
			location.pResource = texture.GetResource();
			location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			location.SubresourceIndex = texture.Subresource(subresource.MipLevel, subresource.ArrayLayer);
			return location;
		}

		// Block-compressed boxes cover whole blocks, as D3D12 requires (a small mip's last block
		// extends past its texel size).
		D3D12_BOX Box(const Rhi::Offset3D& offset, const Rhi::Extent3D& extent, Rhi::Format format)
		{
			const auto block = IsBlockCompressed(format) ? 4u : 1u;
			return { static_cast<UINT>(offset.X), static_cast<UINT>(offset.Y), static_cast<UINT>(offset.Z),
				static_cast<UINT>(offset.X) + AlignUp(extent.Width, block), static_cast<UINT>(offset.Y) + AlignUp(extent.Height, block),
				static_cast<UINT>(offset.Z) + extent.Depth };
		}

	} // namespace

	D3D12Buffer& D3D12CommandList::Scratch(std::uint64_t size)
	{
		Rhi::BufferDesc desc{};
		desc.Size = size;
		desc.Usage = Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination;
		desc.DebugName = "Swim copy repack";
		scratch.push_back(std::make_unique<D3D12Buffer>(state, desc));
		return *scratch.back();
	}

	std::pair<D3D12Buffer*, std::uint64_t> D3D12CommandList::Stage(std::uint64_t size)
	{
		constexpr std::uint64_t blockSize = 4ull << 20;

		if (!staging.empty())
		{
			auto& block = staging.back();
			const auto offset = AlignUp(block.Used, std::uint64_t(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT));

			if (offset + size <= block.Buffer->GetDesc().Size)
			{
				block.Used = offset + size;
				block.Written = true;
				return { block.Buffer.get(), offset };
			}
		}

		StagingBlock block;

		{
			std::scoped_lock lock(allocators->Mutex);
			auto& free = allocators->FreeStaging;
			const auto found = std::find_if(free.begin(), free.end(),
				[&](const auto& buffer)
				{
					return buffer->GetDesc().Size >= size;
				});

			if (found != free.end())
			{
				block.Buffer = std::move(*found);
				free.erase(found);
			}
		}

		if (!block.Buffer)
		{
			Rhi::BufferDesc desc{};
			desc.Size = std::max(size, blockSize);
			desc.Usage = Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination;
			desc.DebugName = "Swim copy staging";
			block.Buffer = std::make_unique<D3D12Buffer>(state, desc);
		}

		block.Used = size;
		block.Written = true;
		staging.push_back(std::move(block));
		return { staging.back().Buffer.get(), 0 };
	}

	void D3D12CommandList::FlushCopies()
	{
		if (pendingCopies.empty())
		{
			return;
		}

		const auto copies = std::move(pendingCopies);
		pendingCopies.clear();
		std::vector<BufferTransition> toSource;
		std::vector<BufferTransition> toDestination;

		for (auto& block : staging)
		{
			if (block.Written)
			{
				toSource.push_back({ block.Buffer.get(), Rhi::ResourceState::CopyDestination, Rhi::ResourceState::CopySource });
				toDestination.push_back({ block.Buffer.get(), Rhi::ResourceState::CopySource, Rhi::ResourceState::CopyDestination });
				block.Written = false;
			}
		}

		RecordBarriers(toSource, {});

		for (const auto& copy : copies)
		{
			list->CopyTextureRegion(&copy.To, copy.X, copy.Y, copy.Z, &copy.From, nullptr);
		}

		// Later rows land in unused ranges; the blocks return to the copy-destination state.
		RecordBarriers(toDestination, {});
	}

	void D3D12CommandList::RetireStaging()
	{
		pendingCopies.clear();

		if (staging.empty())
		{
			return;
		}

		std::scoped_lock lock(allocators->Mutex);

		for (auto& block : staging)
		{
			allocators->RetiredStaging.push_back(std::move(block.Buffer));
		}

		staging.clear();
	}

	void D3D12CommandList::CopyBuffer(Rhi::Buffer& source, Rhi::Buffer& destination, const Rhi::BufferCopyRegion& region)
	{
		FlushCopies();
		list->CopyBufferRegion(static_cast<D3D12Buffer&>(destination).GetResource(), region.DestinationOffset,
			static_cast<D3D12Buffer&>(source).GetResource(), region.SourceOffset, region.Size);
	}

	void D3D12CommandList::CopyTexture(Rhi::Texture& source, Rhi::Texture& destination, const Rhi::TextureCopyRegion& region)
	{
		FlushCopies();
		const auto from = Subresource(static_cast<D3D12Texture&>(source), region.Source);
		const auto to = Subresource(static_cast<D3D12Texture&>(destination), region.Destination);
		const auto box = Box(region.SourceOffset, region.Extent, static_cast<D3D12Texture&>(source).GetDesc().PixelFormat);
		list->CopyTextureRegion(&to, static_cast<UINT>(region.DestinationOffset.X), static_cast<UINT>(region.DestinationOffset.Y),
			static_cast<UINT>(region.DestinationOffset.Z), &from, &box);
	}

	void D3D12CommandList::CopyBufferToTexture(Rhi::Buffer& source, Rhi::Texture& destination, const Rhi::BufferTextureCopyRegion& region)
	{
		auto& buffer = static_cast<D3D12Buffer&>(source);
		auto& texture = static_cast<D3D12Texture&>(destination);
		const auto format = texture.GetDesc().PixelFormat;
		const auto rows = Measure(format, region.Extent, state->Features.UnrestrictedCopyPitch);
		PendingCopy copy;
		copy.To = Subresource(texture, region.Subresource);
		copy.X = static_cast<UINT>(region.TextureOffset.X);
		copy.Y = static_cast<UINT>(region.TextureOffset.Y);
		copy.Z = static_cast<UINT>(region.TextureOffset.Z);

		// Tight rows that are not already pitch- and placement-aligned are repacked on the GPU
		// into staging blocks. Their texture copies wait for one barrier at the next command
		// that could observe them, so a run of copies costs two barriers.
		if (rows.Pitch != rows.RowBytes || region.BufferOffset % rows.Placement != 0)
		{
			const auto [packed, offset] = Stage(std::uint64_t(rows.Pitch) * rows.Rows * rows.Depth);
			const auto lines = rows.Rows * rows.Depth;

			if (rows.Pitch == rows.RowBytes)
			{
				// Rows already pitch-aligned: only the offset moves, in one copy.
				list->CopyBufferRegion(packed->GetResource(), offset, buffer.GetResource(), region.BufferOffset,
					std::uint64_t(rows.RowBytes) * lines);
			}
			else
			{
				for (std::uint32_t line = 0; line < lines; ++line)
				{
					list->CopyBufferRegion(packed->GetResource(), offset + std::uint64_t(line) * rows.Pitch, buffer.GetResource(),
						region.BufferOffset + std::uint64_t(line) * rows.RowBytes, rows.RowBytes);
				}
			}

			copy.From = Footprint(packed->GetResource(), offset, format, region.Extent, rows);
			pendingCopies.push_back(copy);
			return;
		}

		copy.From = Footprint(buffer.GetResource(), region.BufferOffset, format, region.Extent, rows);

		// Behind pending copies to keep their order.
		if (!pendingCopies.empty())
		{
			pendingCopies.push_back(copy);
			return;
		}

		list->CopyTextureRegion(&copy.To, copy.X, copy.Y, copy.Z, &copy.From, nullptr);
	}

	void D3D12CommandList::CopyTextureToBuffer(Rhi::Texture& source, Rhi::Buffer& destination, const Rhi::BufferTextureCopyRegion& region)
	{
		FlushCopies();
		auto& buffer = static_cast<D3D12Buffer&>(destination);
		auto& texture = static_cast<D3D12Texture&>(source);
		const auto format = texture.GetDesc().PixelFormat;
		const auto rows = Measure(format, region.Extent, state->Features.UnrestrictedCopyPitch);
		const auto from = Subresource(texture, region.Subresource);
		const auto box = Box(region.TextureOffset, region.Extent, format);
		const bool direct = rows.Pitch == rows.RowBytes && region.BufferOffset % rows.Placement == 0;
		D3D12Buffer* packed = direct ? nullptr : &Scratch(std::uint64_t(rows.Pitch) * rows.Rows * rows.Depth);
		const auto to =
			Footprint(direct ? buffer.GetResource() : packed->GetResource(), direct ? region.BufferOffset : 0, format, region.Extent, rows);
		list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);

		if (packed)
		{
			Transition(*packed, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::CopySource);
			const auto lines = rows.Rows * rows.Depth;

			for (std::uint32_t line = 0; line < lines; ++line)
			{
				list->CopyBufferRegion(buffer.GetResource(), region.BufferOffset + std::uint64_t(line) * rows.RowBytes,
					packed->GetResource(), std::uint64_t(line) * rows.Pitch, rows.RowBytes);
			}
		}
	}

} // namespace Swim::RhiD3D12
