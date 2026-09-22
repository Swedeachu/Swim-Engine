#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Byte accounting for one stream's pages. Fragmentation is
	// 1 - LargestFreeRange / FreeBytes (zero when nothing, or one block, is free).
	struct GeometryPoolStats
	{
		std::uint32_t Pages = 0;
		std::uint32_t DedicatedPages = 0;
		std::uint64_t ReservedBytes = 0;
		std::uint64_t AllocatedBytes = 0;
		std::uint64_t FreeBytes = 0;
		std::uint64_t LargestFreeRange = 0;
		std::uint64_t FreeRanges = 0;
		double Fragmentation = 0.0;
	};

	struct GeometryHeapStats
	{
		GeometryPoolStats Vertex;
		GeometryPoolStats Index;
		GeometryPoolStats Meshlet;
		std::uint32_t PendingMeshes = 0;
		std::uint32_t RecordedMeshes = 0;
		std::uint32_t UploadingMeshes = 0;
		std::uint32_t ResidentMeshes = 0;
		std::uint32_t RetiringMeshes = 0;
		std::uint64_t PendingUploadBytes = 0;
		std::uint32_t DirtyMetadataRows = 0;
		std::uint32_t DirtySubmeshRows = 0;
		std::uint64_t SubmeshRowsAllocated = 0;
		std::uint64_t SubmeshRowCapacity = 0;
	};
} // namespace Swim::Render
