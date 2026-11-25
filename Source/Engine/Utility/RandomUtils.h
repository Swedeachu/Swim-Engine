#pragma once

#include <cstdint>
#include <limits>
#include <random>

#include <glm/glm.hpp>

namespace Engine
{

	// Deterministic 32-bit hash of an integer grid coordinate (value noise, dithering). The
	// same on every run and platform.
	inline std::uint32_t HashCoords(std::int32_t x, std::int32_t y, std::uint32_t seed = 0)
	{
		std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return h ^ (h >> 16);
	}

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
