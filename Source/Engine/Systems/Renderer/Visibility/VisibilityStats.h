#pragma once
#include <cstdint>

namespace Swim::Render
{
	// GPU visibility counters (one std430 array of 16 uint32). Read asynchronously
	// through VisibilityGraphResources::StatsReadback; never needed for correctness.
	struct VisibilityStats
	{
		static constexpr std::uint32_t MaxLods = 8;

		std::uint32_t Tested = 0;		 // Live rows examined.
		std::uint32_t FrustumCulled = 0; // Live, drawable, outside the view.
		std::uint32_t NotDrawable = 0;	 // Live but hidden or without a mesh.
		std::uint32_t Visible = 0;		 // Instances that emitted draws (or tried to).
		std::uint32_t Draws = 0;		 // Draw commands written.
		std::uint32_t Dropped = 0;		 // Draws lost to full bins.
		std::uint32_t OtherPage = 0;	 // Draws whose index page is not in the frame's page list.
		std::uint32_t Reserved = 0;
		std::uint32_t LodCounts[MaxLods] = {}; // Visible instances per selected LOD.
	};

	static_assert(sizeof(VisibilityStats) == 64);
} // namespace Swim::Render
