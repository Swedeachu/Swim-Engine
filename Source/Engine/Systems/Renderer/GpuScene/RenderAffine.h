#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace Swim::Render
{
	// Row-major 3x4 affine transform: Rows[r * 4 + c]; column 3 is translation.
	// This is exactly the GPU layout of one transform (three float4 rows), so a
	// shader transforms a point with dot(row, float4(p, 1)).
	struct RenderAffine
	{
		std::array<float, 12> Rows{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

		static RenderAffine Identity() { return {}; }

		static RenderAffine Translation(float x, float y, float z)
		{
			RenderAffine result;
			result.Rows[3] = x;
			result.Rows[7] = y;
			result.Rows[11] = z;
			return result;
		}

		// From a column-major 4x4 (glm/GLSL memory order); the last row is dropped.
		static RenderAffine FromColumnMajor(const float* columns)
		{
			RenderAffine result;
			for (int row = 0; row < 3; ++row)
			{
				for (int column = 0; column < 4; ++column)
				{
					result.Rows[row * 4 + column] = columns[column * 4 + row];
				}
			}
			return result;
		}

		std::array<float, 3> TransformPoint(const std::array<float, 3>& p) const
		{
			std::array<float, 3> result{};
			for (int row = 0; row < 3; ++row)
			{
				result[row] = Rows[row * 4] * p[0] + Rows[row * 4 + 1] * p[1] + Rows[row * 4 + 2] * p[2] + Rows[row * 4 + 3];
			}
			return result;
		}

		// Largest column length: the scale a bounding sphere radius needs.
		float MaxScale() const
		{
			float largest = 0.0f;
			for (int column = 0; column < 3; ++column)
			{
				const float x = Rows[column], y = Rows[4 + column], z = Rows[8 + column];
				largest = std::max(largest, std::sqrt(x * x + y * y + z * z));
			}
			return largest;
		}

		bool operator==(const RenderAffine&) const = default;
	};
} // namespace Swim::Render
