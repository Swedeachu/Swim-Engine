#pragma once

#include <random>
#include <limits>

namespace Engine
{

	// Random float in [min, max] inclusive
	inline float RandFloat(float min, float max)
	{
		static thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_real_distribution<float> distribution(min, max);
		return distribution(generator);
	}

	// Random double in [min, max] inclusive
	inline double RandDouble(double min, double max)
	{
		static thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_real_distribution<double> distribution(min, max);
		return distribution(generator);
	}

	// Random int in [min, max] inclusive
	inline int RandInt(int min, int max)
	{
		static thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_int_distribution<int> distribution(min, max);
		return distribution(generator);
	}

	// Random glm::vec2 with each component in [min, max]
	inline glm::vec2 RandVec2(float min, float max)
	{
		return glm::vec2(
			RandFloat(min, max),
			RandFloat(min, max)
		);
	}

	// Random glm::vec3 with each component in [min, max]
	inline glm::vec3 RandVec3(float min, float max)
	{
		return glm::vec3(
			RandFloat(min, max),
			RandFloat(min, max),
			RandFloat(min, max)
		);
	}

}
