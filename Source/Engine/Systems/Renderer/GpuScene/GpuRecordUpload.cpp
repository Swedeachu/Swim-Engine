#include "Engine/Systems/Renderer/GpuScene/GpuRecordUpload.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

#include <cstring>

namespace Swim::Render
{
	std::vector<GpuRecordRun> BuildRecordRuns(std::span<const std::uint32_t> sortedRows)
	{
		std::vector<GpuRecordRun> runs;
		for (auto row : sortedRows)
		{
			if (runs.empty() || runs.back().FirstRow + runs.back().RowCount != row)
			{
				runs.push_back({ row, 0 });
			}
			++runs.back().RowCount;
		}
		return runs;
	}

	GraphPass RecordRowRunsUpload(RenderGraph& graph, const std::string& label, GraphBuffer target, std::uint32_t recordSize,
		std::vector<GpuRecordRun> runs, std::shared_ptr<const std::vector<std::byte>> snapshot)
	{
		using S = Rhi::ResourceState;
		const auto staging = graph.CreateUpload({ snapshot->size(), Rhi::BufferUsage::TransferSource, 16, label + " staging" },
			[snapshot](std::span<std::byte> bytes)
			{
				std::memcpy(bytes.data(), snapshot->data(), snapshot->size());
			});
		return graph.AddPass(
			label, Rhi::QueueType::Transfer,
			[&](RenderGraphBuilder& b)
			{
				b.Read(staging, S::CopySource);
				b.ReadWrite(target, S::CopyDestination);
			},
			[staging, target, recordSize, runs = std::move(runs)](RenderCommandContext& c)
			{
				const auto source = c.GetRange(staging);
				const auto destination = c.GetRange(target);
				std::uint64_t packed = 0;
				for (const auto& run : runs)
				{
					const std::uint64_t bytes = std::uint64_t(run.RowCount) * recordSize;
					c.Commands().CopyBuffer(*source.Buffer, *destination.Buffer,
						{ source.Offset + packed, destination.Offset + std::uint64_t(run.FirstRow) * recordSize, bytes });
					packed += bytes;
				}
			});
	}
} // namespace Swim::Render
