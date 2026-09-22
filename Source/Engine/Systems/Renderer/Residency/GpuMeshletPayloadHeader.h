#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Leading record of a converted mesh's meshlet payload in a GeometryHeap
	// meshlet page. Offsets are bytes from the start of this header; each
	// section starts on a 16-byte boundary. Descriptors are Assets::MeshletDesc
	// records (vertex offset/count, triangle offset/count, four uint32 each).
	struct GpuMeshletPayloadHeader
	{
		std::uint32_t MeshletCount = 0;
		std::uint32_t DescriptorOffset = 0;
		std::uint32_t VertexIndexOffset = 0;
		std::uint32_t VertexIndexBytes = 0;
		std::uint32_t TriangleOffset = 0;
		std::uint32_t TriangleBytes = 0;
		std::uint32_t Padding[2] = {};
	};

	static_assert(sizeof(GpuMeshletPayloadHeader) == 32);
} // namespace Swim::Render
