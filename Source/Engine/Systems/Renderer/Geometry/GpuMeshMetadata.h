#pragma once
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{
	struct GpuMeshLod
	{
		std::uint32_t FirstSubmesh = 0; // Absolute row in the submesh buffer.
		std::uint32_t SubmeshCount = 0;
		float Error = 0.0f;
		std::uint32_t Padding = 0;
	};

	// One std430-compatible row of the GeometryHeap submesh buffer: a ready-made
	// DrawIndexed range against the mesh's index/vertex pages.
	struct GpuSubmeshRecord
	{
		std::uint32_t FirstIndex = 0; // Absolute index within the index page.
		std::uint32_t IndexCount = 0;
		std::int32_t VertexOffset = 0; // Absolute vertex within the vertex page.
		std::uint32_t MaterialSlot = 0;
	};

	// One std430-compatible row of the GeometryHeap metadata buffer. The row
	// index is GpuMeshHandle::Index, which stays stable for the mesh's lifetime
	// and is not reused until its retirement completes. Offsets are expressed in
	// draw units: VertexOffset in vertices (DrawIndexed vertexOffset / vertex
	// pulling base), FirstIndex in indices, MeshletOffset in bytes. Submeshes
	// (draw ranges with material slots) live in a separate row buffer; LODs are
	// submesh ranges. A row whose IndexCount and VertexCount are zero is empty. Page ids index the pages that
	// GeometryHeap::Import exposes; InvalidPage marks an absent stream.
	struct GpuMeshMetadata
	{
		static constexpr std::uint32_t MaxLods = 8;
		static constexpr std::uint32_t InvalidPage = 0xffffffffu;

		std::uint32_t VertexPage = InvalidPage;
		std::uint32_t VertexOffset = 0;
		std::uint32_t VertexCount = 0;
		std::uint32_t VertexStride = 0;
		std::uint32_t IndexPage = InvalidPage;
		std::uint32_t FirstIndex = 0;
		std::uint32_t IndexCount = 0;
		std::uint32_t IndexBytes = 0; // 2 or 4; zero when non-indexed.
		std::uint32_t VertexLayout = 0;
		std::uint32_t LodCount = 0;
		std::uint32_t MeshletPage = InvalidPage;
		std::uint32_t MeshletOffset = 0;
		std::uint32_t MeshletCount = 0;
		std::uint32_t Generation = 0; // Matches the live GpuMeshHandle generation.
		std::uint32_t FirstSubmesh = 0;
		std::uint32_t SubmeshCount = 0;
		GpuMeshLod Lods[MaxLods] = {};
	};

	static_assert(sizeof(GpuMeshLod) == 16);
	static_assert(sizeof(GpuSubmeshRecord) == 16);
	static_assert(sizeof(GpuMeshMetadata) == 192);
	static_assert(offsetof(GpuMeshMetadata, Lods) % 16 == 0);
} // namespace Swim::Render
