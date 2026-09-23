#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Two-phase occlusion culling (item 51). Without occlusion a view uses Single.
	// With occlusion each frame records:
	//   Early - draws the in-frustum objects that were visible last frame (every
	//           in-frustum object after a camera cut or on the first frame);
	//   draw those, then build the HZB from that depth (HzbBuilder);
	//   Late  - tests every in-frustum object against this frame's HZB, draws the
	//           ones not drawn early that are not occluded, and records which objects
	//           are visible for the next frame's early phase.
	// Because the late test uses the current frame's depth, newly visible and
	// teleported objects are never hidden by stale history; they are only drawn a
	// phase later.
	enum class VisibilityPhase : std::uint32_t
	{
		Single = 0,
		Early = 1,
		Late = 2,
	};
} // namespace Swim::Render
