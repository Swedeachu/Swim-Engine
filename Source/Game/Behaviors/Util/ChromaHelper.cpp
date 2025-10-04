#include "PCH.h"
#include "ChromaHelper.h"
#include <cstdio>
#include <string>

namespace Game
{

	std::string ChromaHelper::strf(float value, int precision)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
		return std::string(buffer);
	}

	float ChromaHelper::startHueFromSeed(uint32_t seed)
	{
		const float seed01 = (seed & 0xFFFFu) / 65536.0f;
		const float PHI_CONJ = 0.61803398875f;
		return std::fmod(seed01 * PHI_CONJ, 1.0f);
	}

	glm::vec3 ChromaHelper::HSVtoRGB(float h, float s, float v)
	{
		h = h - std::floor(h);
		s = std::clamp(s, 0.0f, 1.0f);
		v = std::clamp(v, 0.0f, 1.0f);

		const float c = v * s;
		const float h6 = h * 6.0f;
		const float x = c * (1.0f - std::fabs(std::fmod(h6, 2.0f) - 1.0f));
		const float m = v - c;

		float r = 0, g = 0, b = 0;
		const int sector = static_cast<int>(std::floor(h6)) % 6;

		switch (sector)
		{
			case 0: r = c; g = x; b = 0; break;
			case 1: r = x; g = c; b = 0; break;
			case 2: r = 0; g = c; b = x; break;
			case 3: r = 0; g = x; b = c; break;
			case 4: r = x; g = 0; b = c; break;
			case 5: default: r = c; g = 0; b = x; break;
		}

		return glm::vec3(r + m, g + m, b + m);
	}

	void ChromaHelper::Update(double dt)
	{
		elapsed += dt;

		const float cyclesPerSecond = 0.10f; // one rainbow per 10 seconds
		float hue = startHue + static_cast<float>(elapsed) * cyclesPerSecond;
		hue -= std::floor(hue);

		const float saturation = 0.85f;
		const float value = 1.00f;

		currentRGB = HSVtoRGB(hue, saturation, value);
	}

}
