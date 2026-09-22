#pragma once
#include <cstdint>

namespace Swim::Render
{
	// One level of detail: a contiguous range of the mesh's submeshes (relative
	// to its first submesh) plus the screen-space error used by LOD selection.
	struct GeometryLodRange
	{
		std::uint32_t FirstSubmesh = 0;
		std::uint32_t SubmeshCount = 0;
		float Error = 0.0f;
	};
} // namespace Swim::Render
