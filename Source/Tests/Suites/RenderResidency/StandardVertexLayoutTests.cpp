#include "Engine/Systems/Renderer/ForwardPlus/StandardVertex.h"
#include "Engine/Systems/Renderer/Residency/MeshGeometryPayload.h"
#include "Tests/Framework/Test.h"

#include <cstddef>
#include <cstring>

using namespace Swim;
using namespace Swim::Render;
using Assets::VertexElementFormat;
using Assets::VertexSemantic;

// Forward+ pulls cooked static meshes as StandardVertex: the payload the residency
// layer builds from the StaticModelCompiler's packed vertex must carry exactly
// StandardVertexLayoutId, with the same stride and byte layout.
SWIM_TEST("Render.ForwardPlus", "StandardVertexIsTheCookedStaticMeshLayout")
{
	static_assert(
		offsetof(StandardVertex, Normal) == 12 && offsetof(StandardVertex, Tangent) == 24 && offsetof(StandardVertex, TexCoord0) == 40);
	const StandardVertex vertices[3]{ { { 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0, 1 }, { 0, 0 } },
		{ { 1, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0, 1 }, { 1, 0 } }, { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0, -1 }, { 0, 1 } } };
	Assets::MeshAsset mesh;
	mesh.VertexStreams = { { StandardVertexStride, 0, sizeof(vertices) } };
	// Declared out of offset order: the layout hash sorts by offset.
	mesh.VertexAttributes = { { VertexSemantic::TexCoord0, VertexElementFormat::Float32x2, 0, 40 },
		{ VertexSemantic::Position, VertexElementFormat::Float32x3, 0, 0 },
		{ VertexSemantic::Tangent, VertexElementFormat::Float32x4, 0, 24 },
		{ VertexSemantic::Normal, VertexElementFormat::Float32x3, 0, 12 } };
	mesh.VertexBytes.resize(sizeof(vertices));
	std::memcpy(mesh.VertexBytes.data(), vertices, sizeof(vertices));
	const std::uint32_t indices[3]{ 0, 1, 2 };
	mesh.IndexBytes.resize(sizeof(indices));
	std::memcpy(mesh.IndexBytes.data(), indices, sizeof(indices));
	mesh.Primitives = { { 0, 3, 0, 0, {} } };
	mesh.Lods = { { 0, 1, 1.0f } };

	const auto payload = BuildMeshGeometryPayload(mesh);
	SWIM_CHECK_EQUAL(payload.VertexStride, StandardVertexStride);
	SWIM_CHECK_EQUAL(payload.VertexLayout, StandardVertexLayoutId());
	SWIM_REQUIRE_EQUAL(payload.Vertices.size(), sizeof(vertices));
	SWIM_CHECK(std::memcmp(payload.Vertices.data(), vertices, sizeof(vertices)) == 0);
	SWIM_CHECK(payload.IndexFormat == Rhi::IndexType::Uint32);

	// A different layout (no tangents) hashes differently.
	mesh.VertexAttributes.erase(mesh.VertexAttributes.begin() + 2);
	SWIM_CHECK(BuildMeshGeometryPayload(mesh).VertexLayout != StandardVertexLayoutId());
}
