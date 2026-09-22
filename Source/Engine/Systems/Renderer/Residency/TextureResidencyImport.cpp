#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Residency/TextureResidency.h"

#include <cstring>

namespace Swim::Render
{
	TextureGraphResources TextureResidency::Import(RenderGraph& graph)
	{
		using S = Rhi::ResourceState;
		if (importPending)
		{
			throw std::logic_error(name + " has uploads awaiting CommitUploads/AbortUploads");
		}

		TextureGraphResources resources;
		std::vector<GpuTextureHandle> pending;
		textures.ForEach(
			[&](GpuTextureHandle handle, Internal::TextureRecord& record)
			{
				if (record.State != GpuUploadState::PendingUpload)
				{
					return;
				}
				// One staging suballocation and one pass per texture, writing every
				// mip in full, then exported for sampling later in this graph.
				const auto target = graph.ImportTexture(*record.Texture, S::Undefined);
				const auto bytes = record.Bytes;
				const auto mips = record.Mips;
				const auto staging =
					graph.CreateUpload({ bytes->size(), Rhi::BufferUsage::TransferSource, record.Alignment, name + " texture staging" },
						[bytes](std::span<std::byte> destination)
						{
							std::memcpy(destination.data(), bytes->data(), bytes->size());
						});
				const auto pass = graph.AddPass(
					name + " upload texture " + std::to_string(handle.Index), Rhi::QueueType::Transfer,
					[&](RenderGraphBuilder& b)
					{
						b.Read(staging, S::CopySource);
						for (std::uint32_t mip = 0; mip < mips.size(); ++mip)
						{
							b.Write(target, S::CopyDestination, { mip, 1, 0, 1 });
						}
					},
					[staging, target, mips](RenderCommandContext& c)
					{
						const auto source = c.GetRange(staging);
						for (std::uint32_t mip = 0; mip < mips.size(); ++mip)
						{
							Rhi::BufferTextureCopyRegion region;
							region.BufferOffset = source.Offset + mips[mip].Offset;
							region.Subresource = { mip, 0 };
							region.Extent = mips[mip].Extent;
							c.Commands().CopyBufferToTexture(*source.Buffer, c.Get(target, { mip, 1, 0, 1 }), region);
						}
					});
				graph.Export(target, S::ShaderRead);
				resources.Uploads.push_back({ handle, target, pass });
				resources.RecordedBytes += bytes->size();
				pending.push_back(handle);
			});

		for (auto handle : pending)
		{
			textures.Get(handle)->State = GpuUploadState::Recorded;
		}
		recorded = std::move(pending);
		importPending = !recorded.empty();
		return resources;
	}
} // namespace Swim::Render
