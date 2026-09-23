#pragma once
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityPhase.h"

#include <cstdint>
#include <vector>

namespace Swim::Render
{
	struct HzbGraphResources;

	struct VisibilityFrameDesc
	{
		GpuViewRecord View{};
		// GeometryHeap index page ids that this frame's draws may bind, one per slot
		// (at most GpuVisibilityDesc::IndexPageSlots). Draws on other pages are counted
		// in VisibilityStats::OtherPage.
		std::vector<std::uint32_t> IndexPages{ 0 };
		// Adds a readback of the counters for asynchronous diagnostics.
		bool ReadStats = true;
		// Single without occlusion; Early then Late (same view, same graph) with it.
		VisibilityPhase Phase = VisibilityPhase::Single;
		// Late phase: the HZB built from the early phase's depth, with the view's
		// depth convention. Ignored by the other phases.
		const HzbGraphResources* Hzb = nullptr;
		// Fallback for devices without GraphicsCapabilities::IndirectCount: zero every
		// command slot before the cull so DrawIndexedIndirect can issue a bin's whole
		// capacity (unused slots draw zero instances). Costs one staged clear of the
		// command buffer per phase; see VisibilityDraws.h.
		bool ZeroUnusedCommands = false;
	};
} // namespace Swim::Render
