#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{
	// The vertex Clustered Forward+ pulls from a GeometryHeap vertex page (48 bytes):
	// the StaticModelCompiler's packed static vertex. Tangent.w is the bitangent sign
	// (glTF: B = cross(N, T) * w). ClusteredForward.slang reads it as 12 floats.
	struct StandardVertex
	{
		std::array<float, 3> Position{ 0, 0, 0 };
		std::array<float, 3> Normal{ 0, 0, 1 };
		std::array<float, 4> Tangent{ 1, 0, 0, 1 };
		std::array<float, 2> TexCoord0{ 0, 0 };
	};

	static_assert(sizeof(StandardVertex) == 48);

	inline constexpr std::uint32_t StandardVertexStride = 48;

	namespace Internal
	{
		constexpr void HashVertexLayoutWord(std::uint32_t& hash, std::uint32_t value)
		{
			for (int shift = 0; shift < 32; shift += 8)
			{
				hash ^= (value >> shift) & 0xffu;
				hash *= 16777619u;
			}
		}
	} // namespace Internal

	// GeometryMeshDesc::VertexLayout of a cooked static mesh (MeshGeometryPayload's
	// FNV-1a hash of the stride and each (offset, semantic, format) in offset order):
	// Position Float32x3 @0, Normal Float32x3 @12, Tangent Float32x4 @24,
	// TexCoord0 Float32x2 @40. Meshes drawn by Forward+ must use this layout.
	constexpr std::uint32_t StandardVertexLayoutId()
	{
		// (offset, Assets::VertexSemantic, Assets::VertexElementFormat)
		constexpr std::array<std::array<std::uint32_t, 3>, 4> attributes{ { { 0, 0, 1 }, { 12, 1, 1 }, { 24, 2, 2 }, { 40, 3, 0 } } };
		std::uint32_t hash = 2166136261u;
		Internal::HashVertexLayoutWord(hash, StandardVertexStride);
		for (const auto& attribute : attributes)
		{
			for (const auto value : attribute)
			{
				Internal::HashVertexLayoutWord(hash, value);
			}
		}
		return hash ? hash : 1u;
	}
} // namespace Swim::Render
