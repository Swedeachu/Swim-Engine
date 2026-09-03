#pragma once

#include "Engine/Assets/AssetMath.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Swim::Assets
{

	enum class VertexSemantic : std::uint8_t
	{
		Position,
		Normal,
		Tangent,
		TexCoord0,
		TexCoord1,
		Color0,
		Joints0,
		Weights0
	};

	enum class VertexElementFormat : std::uint8_t
	{
		Float32x2,
		Float32x3,
		Float32x4,
		Float16x2,
		Float16x4,
		UNorm8x4,
		SNorm16x4,
		UInt16x4
	};

	enum class IndexElementFormat : std::uint8_t
	{
		UInt16,
		UInt32
	};

	struct VertexAttributeDesc
	{
		VertexSemantic Semantic = VertexSemantic::Position;
		VertexElementFormat Format = VertexElementFormat::Float32x3;
		std::uint16_t StreamIndex = 0;
		std::uint32_t OffsetBytes = 0;
	};

	struct VertexStreamDesc
	{
		std::uint32_t StrideBytes = 0;
		std::uint64_t DataOffsetBytes = 0;
		std::uint64_t DataSizeBytes = 0;
	};

	struct MeshPrimitive
	{
		std::uint32_t FirstIndex = 0;
		std::uint32_t IndexCount = 0;
		std::int32_t VertexOffset = 0;
		std::uint32_t MaterialSlot = 0;
		AssetBounds Bounds{};
	};

	struct MeshLod
	{
		std::uint32_t FirstPrimitive = 0;
		std::uint32_t PrimitiveCount = 0;
		float ScreenCoverage = 1.0f;
	};

	struct MeshletDesc
	{
		std::uint32_t VertexOffset = 0;
		std::uint32_t VertexCount = 0;
		std::uint32_t TriangleOffset = 0;
		std::uint32_t TriangleCount = 0;
	};

	struct MeshAsset
	{
		std::vector<VertexStreamDesc> VertexStreams;
		std::vector<VertexAttributeDesc> VertexAttributes;
		IndexElementFormat IndexFormat = IndexElementFormat::UInt32;
		std::vector<MeshPrimitive> Primitives;
		std::vector<MeshLod> Lods;
		std::vector<MeshletDesc> Meshlets;
		AssetBounds Bounds{};

		// Runtime CPU payload only. These bytes are deliberately backend-neutral
		// and upload-friendly; GPU buffers/heap offsets belong to renderer residency.
		std::vector<std::byte> VertexBytes;
		std::vector<std::byte> IndexBytes;
		std::vector<std::byte> MeshletVertexBytes;
		std::vector<std::byte> MeshletTriangleBytes;
	};

}
