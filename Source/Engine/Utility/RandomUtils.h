#pragma once

#include <random>
#include <limits>

namespace Engine
{

	// Random float in [min, max] inclusive
	inline float randFloat(float min, float max)
	{
		static thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_real_distribution<float> distribution(min, max);
		return distribution(generator);
	}

	// Random double in [min, max] inclusive
	inline double randDouble(double min, double max)
	{
		static thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_real_distribution<double> distribution(min, max);
		return distribution(generator);
	}

	// Random int in [min, max] inclusive
	inline int randInt(int min, int max)
	{
		static thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_int_distribution<int> distribution(min, max);
		return distribution(generator);
	}

}
