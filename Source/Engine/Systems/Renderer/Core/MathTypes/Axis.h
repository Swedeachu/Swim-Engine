#pragma once

#include <utility>

namespace Engine
{

	enum class Axis : uint8_t { None, X, Y, Z };

	inline glm::vec3 AxisDirWorld(Axis a) 
	{
		switch (a)
		{
			case Axis::X: return glm::vec3(1, 0, 0);
			case Axis::Y: return glm::vec3(0, 1, 0);
			case Axis::Z: return glm::vec3(0, 0, 1);
			default:      return glm::vec3(0, 0, 0);
		}
	}

}
