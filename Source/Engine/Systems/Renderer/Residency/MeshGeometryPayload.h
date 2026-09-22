#pragma once
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Systems/Renderer/Geometry/GeometryMeshDesc.h"

#include <vector>

namespace Swim::Render
{
	// GeometryHeap-ready form of a compiled MeshAsset: vertex streams interleaved
	// into one packed stride, primitives as submeshes, LODs as submesh ranges and
	// meshlets packed behind a GpuMeshletPayloadHeader. Index bytes are copied so
	// the payload does not reference the (possibly unloaded) CPU asset.
	struct MeshGeometryPayload
	{
		std::uint32_t VertexLayout = 0; // Stable hash of the packed attribute layout.
		std::uint32_t VertexStride = 0;
		std::vector<std::byte> Vertices;
		Rhi::IndexType IndexFormat = Rhi::IndexType::Uint32;
		std::vector<std::byte> Indices;
		std::vector<GeometrySubmesh> Submeshes;
		std::vector<GeometryLodRange> Lods;
		std::vector<std::byte> Meshlets;
		std::uint32_t MeshletCount = 0;

		// Spans reference this payload; keep it alive for the CreateMesh call.
		GeometryMeshDesc Describe(std::string_view debugName = {}) const;

		std::uint64_t GetUploadBytes() const { return Vertices.size() + Indices.size() + Meshlets.size(); }
	};

	// Throws std::invalid_argument for inconsistent streams, attributes, primitive,
	// LOD or meshlet tables. Meshes with more than GpuMeshMetadata::MaxLods LODs
	// keep their finest MaxLods levels.
	MeshGeometryPayload BuildMeshGeometryPayload(const Assets::MeshAsset& mesh);
} // namespace Swim::Render
