#pragma once
#include <array>

namespace Swim::Render
{
	// Local-space axis-aligned bounds of a render object's mesh, as center and
	// half extents. Culling transforms them with the object's current transform.
	struct RenderBounds
	{
		// Half extents of an object that must never be culled (unknown bounds).
		static constexpr float Unbounded = 1.0e30f;

		std::array<float, 3> Center{ 0.0f, 0.0f, 0.0f };
		std::array<float, 3> Extents{ 0.0f, 0.0f, 0.0f };

		static RenderBounds Infinite() { return { {}, { Unbounded, Unbounded, Unbounded } }; }

		// Min/max corners; an empty box (any min > max) yields Infinite().
		static RenderBounds FromMinMax(const std::array<float, 3>& min, const std::array<float, 3>& max)
		{
			RenderBounds bounds;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!(min[axis] <= max[axis]))
				{
					return Infinite();
				}
				bounds.Center[axis] = (min[axis] + max[axis]) * 0.5f;
				bounds.Extents[axis] = (max[axis] - min[axis]) * 0.5f;
			}
			return bounds;
		}

		bool operator==(const RenderBounds&) const = default;
	};
} // namespace Swim::Render
