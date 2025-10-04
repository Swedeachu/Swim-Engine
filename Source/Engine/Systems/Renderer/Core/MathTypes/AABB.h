#pragma once

#include "Library/glm/glm.hpp"

namespace Engine
{

	struct AABB
	{
		glm::vec3 min{ std::numeric_limits<float>::max() };
		glm::vec3 max{ -std::numeric_limits<float>::max() };
	};

}
