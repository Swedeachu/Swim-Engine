#pragma once

#include <cstdint>

namespace Swim::Render
{
	struct GpuResourceRegistryStats
	{
		std::uint32_t Live = 0;
		std::uint32_t Retiring = 0;		  // Released, waiting for their last GPU use.
		std::uint32_t FreeSlots = 0;	  // Retired slots ready for reuse.
		std::uint32_t SlotHighWater = 0;  // Slots ever created; bounds shader-visible indices.
		std::uint32_t ExhaustedSlots = 0; // Generation space consumed; never reused.
		std::uint32_t MaxSlots = 0;
	};
} // namespace Swim::Render
