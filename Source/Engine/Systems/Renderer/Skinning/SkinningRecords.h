#pragma once
#include <cstddef>
#include <cstdint>

namespace Swim::Render
{
	// GPU skinning records (critical-path item 78), std430 structured-buffer rows
	// mirrored by Shaders/Slang/Skinning/SkinningRecords.slang (a reflection test
	// compares every offset).

	// One source vertex's influences and its morph deltas: four joints (16-bit,
	// packed two per uint, low half first), weights summing to 1 and a range of
	// GpuMorphDelta rows relative to the mesh's first delta.
	struct GpuSkinVertex
	{
		std::uint32_t Joints[2] = { 0, 0 };
		float Weights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
		std::uint32_t MorphFirst = 0;
		std::uint32_t MorphCount = 0;
	};

	// One non-zero morph target displacement of one vertex (sparse, per vertex).
	struct GpuMorphDelta
	{
		std::uint32_t Target = 0;
		float Position[3] = { 0, 0, 0 };
		float Normal[3] = { 0, 0, 0 };
		float Tangent[3] = { 0, 0, 0 };
	};

	// One skinned instance per frame. Palette rows: JointCount current matrices,
	// then JointCount previous ones (three float4 rows each, row-major 3x4).
	// Morph weights: MorphTargetCount current, then as many previous.
	struct GpuSkinDispatch
	{
		std::uint32_t SourceVertex = 0; // First source vertex (StandardVertex and GpuSkinVertex rows).
		std::uint32_t VertexCount = 0;
		std::uint32_t OutputVertex = 0;	  // Absolute vertex in the output GeometryHeap page.
		std::uint32_t PreviousOffset = 0; // Vertices from a current output vertex to its previous position.
		std::uint32_t Palette = 0;		  // First matrix (float4 row / 3).
		std::uint32_t JointCount = 0;
		std::uint32_t MorphWeights = 0; // First weight.
		std::uint32_t MorphTargetCount = 0;
		std::uint32_t SourceMorph = 0; // The mesh's first GpuMorphDelta row.
		std::uint32_t Reserved[3] = { 0, 0, 0 };
	};

	static_assert(sizeof(GpuSkinVertex) == 32);
	static_assert(sizeof(GpuMorphDelta) == 40);
	static_assert(offsetof(GpuMorphDelta, Normal) == 16);
	static_assert(sizeof(GpuSkinDispatch) == 48);
	static_assert(offsetof(GpuSkinDispatch, SourceMorph) == 32);
} // namespace Swim::Render
