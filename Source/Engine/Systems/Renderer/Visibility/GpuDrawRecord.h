#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Per-draw record written next to each indirect command. The command's
	// FirstInstance is the record's slot, so a vertex shader reads
	// DrawRecords[instance id] to find the object row and submesh row.
	struct GpuDrawRecord
	{
		std::uint32_t InstanceRow = 0;
		std::uint32_t SubmeshRow = 0;
	};

	static_assert(sizeof(GpuDrawRecord) == 8);
} // namespace Swim::Render
