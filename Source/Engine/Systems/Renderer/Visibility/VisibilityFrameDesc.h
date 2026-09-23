#pragma once
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"

#include <cstdint>
#include <vector>

namespace Swim::Render
{
	struct VisibilityFrameDesc
	{
		GpuViewRecord View{};
		// GeometryHeap index page ids that this frame's draws may bind, one per slot
		// (at most GpuVisibilityDesc::IndexPageSlots). Draws on other pages are counted
		// in VisibilityStats::OtherPage.
		std::vector<std::uint32_t> IndexPages{ 0 };
		// Adds a readback of the counters for asynchronous diagnostics.
		bool ReadStats = true;
	};
} // namespace Swim::Render
