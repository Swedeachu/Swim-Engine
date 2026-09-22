#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryLodRange.h"
#include "Engine/Systems/Renderer/Geometry/GeometrySubmesh.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::Render
{
	// Compiled mesh payload handed to GeometryHeap::CreateMesh. Bytes are copied at
	// creation, so the spans may be released immediately (for example after a
	// .sasset chunk is decoded). VertexLayout is the caller's packed-format id;
	// the heap stores it verbatim and never interprets vertex contents.
	struct GeometryMeshDesc
	{
		std::uint32_t VertexLayout = 0;
		std::uint32_t VertexStride = 0;
		std::span<const std::byte> Vertices;
		Rhi::IndexType IndexFormat = Rhi::IndexType::Uint32;
		std::span<const std::byte> Indices; // Optional: empty for non-indexed meshes.
		// Draw ranges (e.g. one per material slot). Empty with indices = one submesh
		// over all indices; non-indexed meshes have no submeshes.
		std::span<const GeometrySubmesh> Submeshes;
		// Up to GpuMeshMetadata::MaxLods submesh ranges. Empty = one LOD over all submeshes.
		std::span<const GeometryLodRange> Lods;
		std::span<const std::byte> Meshlets; // Optional opaque meshlet payload.
		std::uint32_t MeshletCount = 0;
		std::string_view DebugName;
	};
} // namespace Swim::Render
