#pragma once
#include <cstdint>
#include <string_view>

namespace Swim::Render
{
	struct GeometryHeapDesc
	{
		// Device-local page sizes per stream. A mesh stream larger than its page
		// size receives a dedicated page of exactly the needed size. Pages must not
		// exceed 4 GiB because metadata offsets are 32-bit.
		std::uint64_t VertexPageSize = 64ull << 20;
		std::uint64_t IndexPageSize = 32ull << 20;
		std::uint64_t MeshletPageSize = 16ull << 20;
		std::uint32_t MaxMeshes = 16384; // Metadata rows and GpuMeshHandle slots.
		std::uint32_t MaxPages = 256;	 // Across all streams, including dedicated pages.
		std::string_view DebugName = "GeometryHeap";
	};
} // namespace Swim::Render
