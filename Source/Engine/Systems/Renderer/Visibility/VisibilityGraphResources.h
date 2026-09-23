#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphReadback.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityBinLayout.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityPhase.h"

#include <optional>

namespace Swim::Render
{
	// Outputs of one GpuVisibility::Record. Draw each bin with
	// DrawIndexedIndirectCount(Commands, range.First * 20, Counts, bin * 4, range.Capacity)
	// after declaring Commands and Counts as IndirectArgument reads, binding the
	// index page of the bin's slot and DrawRecords/GPU Scene buffers as shader reads.
	struct VisibilityGraphResources
	{
		GraphBuffer Commands;	 // Rhi::DrawIndexedIndirectCommand per slot; FirstInstance = slot.
		GraphBuffer DrawRecords; // GpuDrawRecord per slot.
		GraphBuffer Counts;		 // uint32 per bin (may exceed capacity; the draw clamps).
		GraphBuffer Stats;		 // VisibilityStats.
		GraphPass CullPass;
		std::optional<GraphReadback> StatsReadback;
		const VisibilityBinLayout* Bins = nullptr;
		VisibilityPhase Phase = VisibilityPhase::Single;
	};
} // namespace Swim::Render
