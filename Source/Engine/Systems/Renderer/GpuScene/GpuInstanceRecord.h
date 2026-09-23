#pragma once
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{
	// One std430 row of the GPU Scene instance buffer (Shaders/Slang/GpuScene/
	// GpuSceneRecords.slang mirrors it; a reflection test compares offsets). The
	// row index is RenderObjectHandle::Index. It changes only when the object's
	// mesh, material set, flags, skin, LOD bias or bounds change, never for
	// transform-only updates, which touch GpuTransformRecord alone.
	struct GpuInstanceRecord
	{
		static constexpr std::uint32_t InvalidIndex = 0xffffffffu;

		float LocalCenter[3] = { 0.0f, 0.0f, 0.0f };  // Local-space bounds center.
		std::uint32_t MeshIndex = InvalidIndex;		  // GeometryHeap metadata row.
		float LocalExtents[3] = { 0.0f, 0.0f, 0.0f }; // Local-space half extents.
		std::uint32_t MeshGeneration = 0;			  // Compare with GpuMeshMetadata::Generation.
		std::uint32_t TransformIndex = InvalidIndex;  // GpuTransformRecord row.
		std::uint32_t MaterialSet = InvalidIndex;	  // Producer-defined material-set id.
		std::uint32_t ObjectId = 0;					  // Producer id for picking/debug output.
		std::uint32_t Flags = 0;					  // RenderObjectFlags; zero marks a dead row.
		std::uint32_t SkinIndex = InvalidIndex;
		float LodBias = 0.0f;
		std::uint32_t Generation = 0; // RenderObjectHandle generation; zero when dead.
		std::uint32_t Reserved = 0;
	};

	static_assert(sizeof(GpuInstanceRecord) == 64);
	static_assert(offsetof(GpuInstanceRecord, MeshIndex) == 12);
	static_assert(offsetof(GpuInstanceRecord, LocalExtents) == 16);
	static_assert(offsetof(GpuInstanceRecord, TransformIndex) == 32);
} // namespace Swim::Render
