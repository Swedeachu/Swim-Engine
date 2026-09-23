#pragma once
#include <cstdint>

namespace Swim::Render
{
	// GPU visibility counters (one std430 array of 20 uint32). Read asynchronously
	// through VisibilityGraphResources::StatsReadback; never needed for correctness.
	// Every live row lands in exactly one bucket per phase:
	//   Single: FrustumCulled + NotDrawable + Visible == Tested
	//   Early:  FrustumCulled + NotDrawable + Visible + Deferred == Tested
	//   Late:   FrustumCulled + NotDrawable + Visible + AlreadyDrawn + Occluded == Tested
	struct VisibilityStats
	{
		static constexpr std::uint32_t MaxLods = 8;

		std::uint32_t Tested = 0;		 // Live rows examined.
		std::uint32_t FrustumCulled = 0; // Live, drawable, outside the view.
		std::uint32_t NotDrawable = 0;	 // Live but hidden or without a mesh.
		std::uint32_t Visible = 0;		 // Instances that emitted draws (or tried to) in this phase.
		std::uint32_t Draws = 0;		 // Draw commands written.
		std::uint32_t Dropped = 0;		 // Draws lost to full bins.
		std::uint32_t OtherPage = 0;	 // Draws whose index page is not in the frame's page list.
		std::uint32_t Occluded = 0;		 // Late: in the frustum, not drawn early, hidden by the HZB.
		std::uint32_t Deferred = 0;		 // Early: in the frustum but not visible last frame (left to the late phase).
		std::uint32_t AlreadyDrawn = 0;	 // Late: drawn by the early phase.
		std::uint32_t Reserved[2] = {};
		std::uint32_t LodCounts[MaxLods] = {}; // Visible instances per selected LOD.
	};

	static_assert(sizeof(VisibilityStats) == 80);
} // namespace Swim::Render
