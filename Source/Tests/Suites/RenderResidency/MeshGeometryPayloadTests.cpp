#include "Engine/Systems/Renderer/Geometry/GpuMeshMetadata.h"
#include "Engine/Systems/Renderer/Residency/GpuMeshletPayloadHeader.h"
#include "Engine/Systems/Renderer/Residency/MeshGeometryPayload.h"
#include "Tests/Framework/Test.h"

#include <cstring>

using namespace Swim;
using namespace Swim::Render;
using Assets::VertexElementFormat;
using Assets::VertexSemantic;

namespace
{
	Assets::MeshAsset TwoStreamQuad()
	{
		Assets::MeshAsset mesh;
		// Stream 0: float3 positions (12 bytes); stream 1: float2 uvs (8 bytes).
		mesh.VertexStreams = { { 12, 0, 48 }, { 8, 48, 32 } };
		mesh.VertexAttributes = { { VertexSemantic::Position, VertexElementFormat::Float32x3, 0, 0 },
			{ VertexSemantic::TexCoord0, VertexElementFormat::Float32x2, 1, 0 } };
		mesh.VertexBytes.resize(80);
		for (std::size_t i = 0; i < mesh.VertexBytes.size(); ++i)
		{
			mesh.VertexBytes[i] = static_cast<std::byte>(i);
		}
		mesh.IndexFormat = Assets::IndexElementFormat::UInt16;
		mesh.IndexBytes.resize(12, std::byte{ 1 });
		mesh.Primitives = { { 0, 3, 0, 4, {} }, { 3, 3, 1, 7, {} } };
		mesh.Lods = { { 0, 2, 1.0f }, { 1, 1, 0.25f } };
		return mesh;
	}
} // namespace

SWIM_TEST("Render.MeshGeometryPayload", "InterleavesStreamsAndMapsPrimitivesAndLods")
{
	const auto mesh = TwoStreamQuad();
	const auto payload = BuildMeshGeometryPayload(mesh);
	SWIM_CHECK_EQUAL(payload.VertexStride, 20u);
	SWIM_REQUIRE_EQUAL(payload.Vertices.size(), 80u);
	for (std::size_t v = 0; v < 4; ++v)
	{
		SWIM_CHECK(std::memcmp(payload.Vertices.data() + v * 20, mesh.VertexBytes.data() + v * 12, 12) == 0);
		SWIM_CHECK(std::memcmp(payload.Vertices.data() + v * 20 + 12, mesh.VertexBytes.data() + 48 + v * 8, 8) == 0);
	}
	SWIM_CHECK(payload.IndexFormat == Rhi::IndexType::Uint16);
	SWIM_CHECK(payload.Indices == mesh.IndexBytes);
	SWIM_REQUIRE_EQUAL(payload.Submeshes.size(), 2u);
	SWIM_CHECK_EQUAL(payload.Submeshes[1].FirstIndex, 3u);
	SWIM_CHECK_EQUAL(payload.Submeshes[1].VertexOffset, 1);
	SWIM_CHECK_EQUAL(payload.Submeshes[1].MaterialSlot, 7u);
	SWIM_REQUIRE_EQUAL(payload.Lods.size(), 2u);
	SWIM_CHECK_EQUAL(payload.Lods[1].FirstSubmesh, 1u);
	SWIM_CHECK_EQUAL(payload.Lods[1].Error, 0.25f);
	SWIM_CHECK_EQUAL(payload.GetUploadBytes(), 92u);

	const auto desc = payload.Describe("quad");
	SWIM_CHECK_EQUAL(desc.VertexStride, 20u);
	SWIM_CHECK_EQUAL(desc.Submeshes.size(), 2u);
	SWIM_CHECK(desc.DebugName == "quad");
}

SWIM_TEST("Render.MeshGeometryPayload", "LayoutIdsAreStableAndLayoutSensitive")
{
	const auto mesh = TwoStreamQuad();
	const auto first = BuildMeshGeometryPayload(mesh).VertexLayout;
	SWIM_CHECK(first != 0u);
	SWIM_CHECK_EQUAL(BuildMeshGeometryPayload(mesh).VertexLayout, first);

	auto changed = mesh;
	changed.VertexAttributes[1].Format = VertexElementFormat::Float16x2;
	SWIM_CHECK(BuildMeshGeometryPayload(changed).VertexLayout != first);

	// The same packed layout expressed as one interleaved stream hashes identically.
	Assets::MeshAsset single;
	single.VertexStreams = { { 20, 0, 80 } };
	single.VertexAttributes = { { VertexSemantic::Position, VertexElementFormat::Float32x3, 0, 0 },
		{ VertexSemantic::TexCoord0, VertexElementFormat::Float32x2, 0, 12 } };
	single.VertexBytes.resize(80);
	SWIM_CHECK_EQUAL(BuildMeshGeometryPayload(single).VertexLayout, first);
}

SWIM_TEST("Render.MeshGeometryPayload", "RejectsInconsistentTablesAndTruncatesLods")
{
	auto mismatch = TwoStreamQuad();
	mismatch.VertexStreams[1].DataSizeBytes = 24; // Three vertices instead of four.
	SWIM_CHECK_THROWS(BuildMeshGeometryPayload(mismatch), std::invalid_argument);

	auto outside = TwoStreamQuad();
	outside.VertexAttributes[1].OffsetBytes = 4; // float2 at +4 overruns an 8-byte stride.
	SWIM_CHECK_THROWS(BuildMeshGeometryPayload(outside), std::invalid_argument);

	auto overrun = TwoStreamQuad();
	overrun.VertexStreams[1].DataOffsetBytes = 64;
	SWIM_CHECK_THROWS(BuildMeshGeometryPayload(overrun), std::invalid_argument);

	Assets::MeshAsset empty;
	SWIM_CHECK_THROWS(BuildMeshGeometryPayload(empty), std::invalid_argument);

	auto noPrimitives = TwoStreamQuad();
	noPrimitives.Primitives.clear();
	SWIM_CHECK_THROWS(BuildMeshGeometryPayload(noPrimitives), std::invalid_argument);

	auto manyLods = TwoStreamQuad();
	manyLods.Lods.assign(12, { 0, 1, 1.0f });
	SWIM_CHECK_EQUAL(BuildMeshGeometryPayload(manyLods).Lods.size(), std::size_t(GpuMeshMetadata::MaxLods));
}

SWIM_TEST("Render.MeshGeometryPayload", "PacksMeshletsBehindAnAlignedHeader")
{
	auto mesh = TwoStreamQuad();
	mesh.Meshlets = { { 0, 4, 0, 2 }, { 4, 3, 2, 1 } };
	mesh.MeshletVertexBytes.resize(28, std::byte{ 0x11 });
	mesh.MeshletTriangleBytes.resize(9, std::byte{ 0x22 });
	const auto payload = BuildMeshGeometryPayload(mesh);
	SWIM_CHECK_EQUAL(payload.MeshletCount, 2u);
	GpuMeshletPayloadHeader header;
	SWIM_REQUIRE(payload.Meshlets.size() >= sizeof(header));
	std::memcpy(&header, payload.Meshlets.data(), sizeof(header));
	SWIM_CHECK_EQUAL(header.MeshletCount, 2u);
	SWIM_CHECK_EQUAL(header.DescriptorOffset % 16, 0u);
	SWIM_CHECK_EQUAL(header.VertexIndexOffset % 16, 0u);
	SWIM_CHECK_EQUAL(header.TriangleOffset % 16, 0u);
	SWIM_CHECK_EQUAL(header.VertexIndexBytes, 28u);
	SWIM_CHECK_EQUAL(header.TriangleBytes, 9u);
	SWIM_CHECK_EQUAL(payload.Meshlets.size(), std::size_t(header.TriangleOffset + 9));
	Assets::MeshletDesc second;
	std::memcpy(&second, payload.Meshlets.data() + header.DescriptorOffset + sizeof(second), sizeof(second));
	SWIM_CHECK_EQUAL(second.VertexOffset, 4u);
	SWIM_CHECK_EQUAL(second.TriangleCount, 1u);
	SWIM_CHECK(payload.Meshlets[header.TriangleOffset] == std::byte{ 0x22 });
}
