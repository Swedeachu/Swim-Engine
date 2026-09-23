#pragma once
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Swim::Render
{
	// A contiguous range of rows copied by one CopyBuffer.
	struct GpuRecordRun
	{
		std::uint32_t FirstRow = 0;
		std::uint32_t RowCount = 0;
	};

	// Groups sorted, unique rows into maximal contiguous runs.
	std::vector<GpuRecordRun> BuildRecordRuns(std::span<const std::uint32_t> sortedRows);

	// Records one transfer pass copying `runs` of fixed-size records from a
	// snapshot (packed in run order) into `target` at row * recordSize, as
	// preserving partial copies. One staging allocation serves every run.
	GraphPass RecordRowRunsUpload(RenderGraph& graph, const std::string& label, GraphBuffer target, std::uint32_t recordSize,
		std::vector<GpuRecordRun> runs, std::shared_ptr<const std::vector<std::byte>> snapshot);
} // namespace Swim::Render
