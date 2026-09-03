#pragma once

#include <array>
#include <limits>

namespace Swim::Assets
{

	struct AssetBounds
	{
		std::array<float, 3> Min
		{
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max()
		};
		std::array<float, 3> Max
		{
			-std::numeric_limits<float>::max(),
			-std::numeric_limits<float>::max(),
			-std::numeric_limits<float>::max()
		};
	};

	struct AssetTransform
	{
		std::array<float, 3> Translation{ 0.0f, 0.0f, 0.0f };
		std::array<float, 4> Rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		std::array<float, 3> Scale{ 1.0f, 1.0f, 1.0f };
	};

}
