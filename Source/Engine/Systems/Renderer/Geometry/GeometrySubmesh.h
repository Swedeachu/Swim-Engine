#pragma once
#include <cstdint>

namespace Swim::Render
{
	// One draw range of a mesh (normally one compiled MeshAsset primitive). Index
	// and vertex offsets are relative to the mesh's own index/vertex data; the
	// heap rebases them into absolute page offsets in GpuSubmeshRecord.
	struct GeometrySubmesh
	{
		std::uint32_t FirstIndex = 0;
		std::uint32_t IndexCount = 0;
		std::int32_t VertexOffset = 0;
		std::uint32_t MaterialSlot = 0;
	};
} // namespace Swim::Render
