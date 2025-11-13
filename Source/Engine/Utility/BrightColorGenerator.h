#pragma once
#include <algorithm>
#include <cmath>
#include "Library/glm/glm.hpp"
#include "RandomUtils.h"

namespace Engine
{

	inline float Clamp01(float x) { return std::max(0.0f, std::min(1.0f, x)); }

	inline float SRGBToLinear(float c)
	{
		// IEC 61966-2-1:1999 sRGB EOTF
		return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
	}

	inline glm::vec3 HSVtoRGB(float h, float s, float v)
	{
		// h in [0,1), s in [0,1], v in [0,1]
		h = h - std::floor(h); // wrap
		s = Clamp01(s);
		v = Clamp01(v);

		if (s <= 0.00001f) { return glm::vec3(v, v, v); }

		float hf = h * 6.0f;
		int   i = static_cast<int>(std::floor(hf));
		float f = hf - static_cast<float>(i);

		float p = v * (1.0f - s);
		float q = v * (1.0f - s * f);
		float t = v * (1.0f - s * (1.0f - f));

		switch (i % 6)
		{
			case 0: return { v, t, p };
			case 1: return { q, v, p };
			case 2: return { p, v, t };
			case 3: return { p, q, v };
			case 4: return { t, p, v };
			default:return { v, p, q };
		}
	}

	inline float GetSaturationApprox(const glm::vec3& rgb)
	{
		float mx = std::max(rgb.r, std::max(rgb.g, rgb.b));
		float mn = std::min(rgb.r, std::min(rgb.g, rgb.b));
		if (mx <= 0.0001f) { return 0.0f; }
		return (mx - mn) / mx;
	}

	inline float GetPerceivedLuminance(const glm::vec3& rgb_srgb)
	{
		// Convert sRGB to linear for luminance
		float r = SRGBToLinear(Clamp01(rgb_srgb.r));
		float g = SRGBToLinear(Clamp01(rgb_srgb.g));
		float b = SRGBToLinear(Clamp01(rgb_srgb.b));
		float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;

		return Clamp01(y);
	}

	// ---------- main API ----------
	inline glm::vec3 RandomBrightColor(float minSaturation = 0.65f, float minValue = 0.85f)
	{
		minSaturation = Clamp01(minSaturation);
		minValue = Clamp01(minValue);

		float h = RandFloat(0.0f, 1.0f);
		float s = RandFloat(minSaturation, 1.0f);
		float v = RandFloat(minValue, 1.0f);

		glm::vec3 c = HSVtoRGB(h, s, v);

		// Safety bump if the random pick still landed too gray or too dark perceptually
		if (GetSaturationApprox(c) < minSaturation || GetPerceivedLuminance(c) < minValue)
		{
			s = std::max(s, std::min(0.9f, minSaturation + 0.1f));
			v = std::max(v, std::min(0.95f, minValue + 0.1f));
			c = HSVtoRGB(h, s, v);
		}

		return c; // glm::vec3(R,G,B) in sRGB
	}

}
