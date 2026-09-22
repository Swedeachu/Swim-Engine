#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;

		struct UploadChunk
		{
			Internal::GeometryPayload Bytes;
			std::uint64_t StagingOffset = 0;
			std::uint64_t DestinationOffset = 0;
		};

		struct PageUpload
		{
			std::vector<UploadChunk> Chunks;
			std::uint64_t StagingBytes = 0;
		};

		struct RowRun
		{
			std::uint32_t FirstRow = 0;
			std::uint32_t RowCount = 0;
			std::uint64_t StagingOffset = 0;
		};

		constexpr std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		// One staging suballocation and one transfer pass per page; every chunk is a
		// preserving partial copy, so the page's other meshes stay intact.
		GraphPass RecordPageUpload(RenderGraph& graph, const std::string& label, GraphBuffer page, PageUpload upload)
		{
			auto chunks = std::make_shared<const std::vector<UploadChunk>>(std::move(upload.Chunks));
			const auto staging = graph.CreateUpload({ upload.StagingBytes, Rhi::BufferUsage::TransferSource, 4, label + " staging" },
				[chunks](std::span<std::byte> bytes)
				{
					for (const auto& chunk : *chunks)
					{
						std::memcpy(bytes.data() + chunk.StagingOffset, chunk.Bytes->data(), chunk.Bytes->size());
					}
				});
			return graph.AddPass(
				label, Rhi::QueueType::Transfer,
				[&](RenderGraphBuilder& b)
				{
					b.Read(staging, S::CopySource);
					b.ReadWrite(page, S::CopyDestination);
				},
				[staging, page, chunks](RenderCommandContext& c)
				{
					const auto source = c.GetRange(staging);
					const auto destination = c.GetRange(page);
					for (const auto& chunk : *chunks)
					{
						c.Commands().CopyBuffer(*source.Buffer, *destination.Buffer,
							{ source.Offset + chunk.StagingOffset, destination.Offset + chunk.DestinationOffset, chunk.Bytes->size() });
					}
				});
		}
	} // namespace

	GeometryGraphResources GeometryHeap::Import(RenderGraph& graph)
	{
		if (importPending)
		{
			throw std::logic_error("GeometryHeap has uploads awaiting CommitUploads/AbortUploads");
		}

		GeometryGraphResources resources;
		resources.Pages.resize(pages.size());
		for (std::uint32_t p = 0; p < pages.size(); ++p)
		{
			if (pages[p].Buffer)
			{
				// Unused page bytes are never read, so every page (new ones too) is
				// imported as initialized in its resting state; buffers have no layout.
				resources.Pages[p] = graph.ImportBuffer(*pages[p].Buffer, GetRestingState(pages[p].Stream));
			}
		}
		resources.Metadata = graph.ImportBuffer(*metadataBuffer, S::ShaderRead);

		std::map<std::uint32_t, PageUpload> uploads;
		const auto stage = [&](const Internal::GeometryAllocation& allocation, const Internal::GeometryPayload& bytes)
		{
			if (!allocation.IsValid() || !bytes)
			{
				return;
			}
			auto& upload = uploads[allocation.Page];
			upload.StagingBytes = AlignUp(upload.StagingBytes, 4);
			upload.Chunks.push_back({ bytes, upload.StagingBytes, allocation.Range.Offset });
			upload.StagingBytes += bytes->size();
			resources.RecordedBytes += bytes->size();
		};
		std::vector<GpuMeshHandle> recorded;
		meshes.ForEach(
			[&](GpuMeshHandle handle, Internal::GeometryMeshRecord& record)
			{
				if (record.State == GeometryResidency::PendingUpload)
				{
					stage(record.Vertex, record.VertexBytes);
					stage(record.Index, record.IndexBytes);
					stage(record.Meshlet, record.MeshletBytes);
					recorded.push_back(handle);
				}
			});

		// Graph declarations validate before any heap state changes.
		for (auto& [page, upload] : uploads)
		{
			resources.UploadPasses.push_back(
				RecordPageUpload(graph, name + " upload page " + std::to_string(page), resources.Pages[page], std::move(upload)));
		}

		std::vector<std::uint32_t> rows = dirtyRows;
		std::sort(rows.begin(), rows.end());
		if (!rows.empty())
		{
			auto snapshot = std::make_shared<std::vector<GpuMeshMetadata>>();
			std::vector<RowRun> runs;
			for (auto row : rows)
			{
				if (runs.empty() || runs.back().FirstRow + runs.back().RowCount != row)
				{
					runs.push_back({ row, 0, snapshot->size() * sizeof(GpuMeshMetadata) });
				}
				++runs.back().RowCount;
				snapshot->push_back(metadata[row]);
			}
			const auto bytes = snapshot->size() * sizeof(GpuMeshMetadata);
			const auto staging = graph.CreateUpload({ bytes, Rhi::BufferUsage::TransferSource, 16, name + " metadata staging" },
				[snapshot](std::span<std::byte> destination)
				{
					std::memcpy(destination.data(), snapshot->data(), snapshot->size() * sizeof(GpuMeshMetadata));
				});
			const auto target = resources.Metadata;
			resources.UploadPasses.push_back(graph.AddPass(
				name + " upload metadata", Rhi::QueueType::Transfer,
				[&](RenderGraphBuilder& b)
				{
					b.Read(staging, S::CopySource);
					b.ReadWrite(target, S::CopyDestination);
				},
				[staging, target, runs](RenderCommandContext& c)
				{
					const auto source = c.GetRange(staging);
					const auto destination = c.GetRange(target);
					for (const auto& run : runs)
					{
						c.Commands().CopyBuffer(*source.Buffer, *destination.Buffer,
							{ source.Offset + run.StagingOffset, destination.Offset + std::uint64_t(run.FirstRow) * sizeof(GpuMeshMetadata),
								std::uint64_t(run.RowCount) * sizeof(GpuMeshMetadata) });
					}
				}));
			resources.RecordedBytes += bytes;
		}

		for (auto handle : recorded)
		{
			meshes.Get(handle)->State = GeometryResidency::Recorded;
		}
		for (auto row : rows)
		{
			dirty[row] = false;
		}
		dirtyRows.clear();
		recordedMeshes = std::move(recorded);
		recordedRows = std::move(rows);
		resources.RecordedMeshes = static_cast<std::uint32_t>(recordedMeshes.size());
		importPending = !resources.UploadPasses.empty();
		return resources;
	}

	void GeometryHeap::CommitUploads(Rhi::TimelinePoint completion)
	{
		if (!importPending)
		{
			return;
		}
		if (!completion.Semaphore)
		{
			throw std::invalid_argument("GeometryHeap upload commit needs the graph's completion point");
		}
		for (auto handle : recordedMeshes)
		{
			if (auto* record = meshes.Get(handle))
			{
				record->State = GeometryResidency::Uploading;
				record->Upload = completion;
				// The executor already copied the payload into staging.
				record->VertexBytes.reset();
				record->IndexBytes.reset();
				record->MeshletBytes.reset();
			}
		}
		recordedMeshes.clear();
		recordedRows.clear();
		importPending = false;
	}

	void GeometryHeap::AbortUploads()
	{
		for (auto handle : recordedMeshes)
		{
			if (auto* record = meshes.Get(handle))
			{
				record->State = GeometryResidency::PendingUpload;
			}
		}
		for (auto row : recordedRows)
		{
			MarkDirty(row);
		}
		recordedMeshes.clear();
		recordedRows.clear();
		importPending = false;
	}
} // namespace Swim::Render
