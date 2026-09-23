#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Persistent per-row LOD history for hysteresis. Generation must match the
	// row's current GpuInstanceRecord::Generation, so reused rows start fresh.
	struct GpuLodState
	{
		std::uint32_t Generation = 0;
		std::uint32_t Lod = 0;
	};

	static_assert(sizeof(GpuLodState) == 8);
} // namespace Swim::Render
