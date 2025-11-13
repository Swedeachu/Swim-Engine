#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

	struct AABB
	{
		glm::vec3 min{ std::numeric_limits<float>::max() };
		glm::vec3 max{ -std::numeric_limits<float>::max() };
	};

	inline bool AABBInsideAABB(const AABB& inner, const AABB& outer)
	{
		const glm::vec3& iMin = inner.min;
		const glm::vec3& iMax = inner.max;
		const glm::vec3& oMin = outer.min;
		const glm::vec3& oMax = outer.max;

		return (iMin.x >= oMin.x && iMin.y >= oMin.y && iMin.z >= oMin.z) &&
			(iMax.x <= oMax.x && iMax.y <= oMax.y && iMax.z <= oMax.z);
	}

	inline bool PointInsideAABB(const glm::vec3& p, const AABB& aabb)
	{
		const glm::vec3& mn = aabb.min;
		const glm::vec3& mx = aabb.max;
		return (p.x >= mn.x && p.x <= mx.x) &&
			(p.y >= mn.y && p.y <= mx.y) &&
			(p.z >= mn.z && p.z <= mx.z);
	}

}
