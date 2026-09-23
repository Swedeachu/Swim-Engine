#pragma once
#include <cstddef>

namespace Swim::Render
{
	// One std430 row of the GPU Scene transform buffer: this frame's and the
	// previous frame's world transform as row-major 3x4 affine rows (three float4
	// each). Previous feeds motion vectors and history reprojection; for an object
	// that did not move in the previous frame it equals Current.
	struct GpuTransformRecord
	{
		float Current[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
		float Previous[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
	};

	static_assert(sizeof(GpuTransformRecord) == 96);
	static_assert(offsetof(GpuTransformRecord, Previous) == 48);
} // namespace Swim::Render
