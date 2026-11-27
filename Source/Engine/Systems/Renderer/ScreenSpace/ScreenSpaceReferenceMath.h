#pragma once
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"

#include <cmath>
#include <cstdint>
#include <optional>

// Vector helpers shared by the CPU reference's translation units.
namespace Swim::Render::ScreenSpace::ReferenceMath
{

	inline float Dot(const Float3& a, const Float3& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	inline float Length(const Float3& v)
	{
		return std::sqrt(Dot(v, v));
	}

	inline Float3 Scale(const Float3& v, float s)
	{
		return { v[0] * s, v[1] * s, v[2] * s };
	}

	inline Float3 Subtract(const Float3& a, const Float3& b)
	{
		return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
	}

	// Rows 0-2 of a row-major affine transform applied to a point.
	inline Float3 TransformPoint(const float (&rows)[12], const Float3& p)
	{
		Float3 result{};

		for (int r = 0; r < 3; ++r)
		{
			result[r] = rows[r * 4] * p[0] + rows[r * 4 + 1] * p[1] + rows[r * 4 + 2] * p[2] + rows[r * 4 + 3];
		}

		return result;
	}

	inline std::optional<Float3> ViewPositionAt(
		const GpuScreenSpaceParams& params, const ScalarImage& depth, std::uint32_t x, std::uint32_t y)
	{
		return ViewPosition(params, float(x) + 0.5f, float(y) + 0.5f, depth.At(x, y));
	}

} // namespace Swim::Render::ScreenSpace::ReferenceMath
