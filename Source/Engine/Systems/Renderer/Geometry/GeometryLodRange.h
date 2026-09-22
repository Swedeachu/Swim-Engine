#pragma once
#include <cstdint>

namespace Swim::Render
{
	// One level of detail as an index range relative to the mesh's own indices,
	// plus the screen-space error used by later LOD selection.
	struct GeometryLodRange
	{
		std::uint32_t FirstIndex = 0;
		std::uint32_t IndexCount = 0;
		float Error = 0.0f;
	};
} // namespace Swim::Render
