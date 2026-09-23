#pragma once
#include <cstdint>

namespace Swim::Render
{
	// A bin's slice of the indirect command and draw-record buffers (in draws).
	// Bin index = materialBin * pageSlotCount + indexPageSlot.
	struct VisibilityBinRange
	{
		std::uint32_t First = 0;
		std::uint32_t Capacity = 0;
	};

	static_assert(sizeof(VisibilityBinRange) == 8);
} // namespace Swim::Render
