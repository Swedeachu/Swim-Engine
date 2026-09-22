#pragma once

#include <cstdint>
#include <string_view>

namespace Swim::Render
{
	struct GpuResourceRegistryDesc
	{
		// Upper bound on simultaneously live + retiring slots (for example a
		// bindless table size or metadata buffer row count). Slot indices are
		// always below this value.
		std::uint32_t MaxSlots = UINT32_MAX - 1;
		std::string_view DebugName = "GpuResourceRegistry";
	};
} // namespace Swim::Render
