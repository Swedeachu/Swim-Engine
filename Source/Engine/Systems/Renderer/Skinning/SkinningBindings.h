#pragma once
#include <cstdint>

namespace Swim::Render
{
	// Descriptor contract (space 0) of SwimSkinning: 64-thread groups along the
	// instance's vertices (x), one group row per instance of the dispatch (y).
	inline constexpr std::uint32_t SkinningThreadGroupSize = 64;

	struct SkinningBindings
	{
		static constexpr std::uint32_t Dispatches = 0;	   // StructuredBuffer<GpuSkinDispatch>.
		static constexpr std::uint32_t SourceVertices = 1; // StructuredBuffer<float>: StandardVertex, 12 floats each.
		static constexpr std::uint32_t SkinVertices = 2;   // StructuredBuffer<GpuSkinVertex>.
		static constexpr std::uint32_t MorphDeltas = 3;	   // StructuredBuffer<GpuMorphDelta>.
		static constexpr std::uint32_t Palettes = 4;	   // StructuredBuffer<float4>: three rows per matrix.
		static constexpr std::uint32_t MorphWeights = 5;   // StructuredBuffer<float>.
		static constexpr std::uint32_t Output = 6;		   // RWStructuredBuffer<float>: the GeometryHeap vertex page.
		static constexpr std::uint32_t Count = 7;
		// Push constants: the first GpuSkinDispatch row of this dispatch (uint).
		static constexpr std::uint32_t PushConstantBytes = 4;
	};
} // namespace Swim::Render
