#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

	struct Ray
	{
		glm::vec3 origin{ 0.0f };
		glm::vec3 dir{ 0.0f, 0.0f, 1.0f }; // should be normalized
		glm::vec3 invDir{ INFINITY, INFINITY, INFINITY }; // 1/dir (handles 0 by INF)
		glm::vec3 debugColor{ 1.0f, 0.0f, 0.0f };
		int sign[3]{ 0,0,0 }; // dir.x < 0 ? 1 : 0, etc.

		Ray() = default;

		Ray(const glm::vec3& o, const glm::vec3& d) : origin(o)
		{
			// Normalize dir safely; if zero-length, keep default +Z
			float len = glm::length(d);
			dir = (len > 0.0f) ? (d / len) : glm::vec3(0, 0, 1);

			invDir.x = (dir.x != 0.0f) ? 1.0f / dir.x : std::numeric_limits<float>::infinity();
			invDir.y = (dir.y != 0.0f) ? 1.0f / dir.y : std::numeric_limits<float>::infinity();
			invDir.z = (dir.z != 0.0f) ? 1.0f / dir.z : std::numeric_limits<float>::infinity();

			sign[0] = (dir.x < 0.0f);
			sign[1] = (dir.y < 0.0f);
			sign[2] = (dir.z < 0.0f);
		}
	};

}
