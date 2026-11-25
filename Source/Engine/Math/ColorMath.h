#pragma once

#include <cmath>

// Colour-space helpers shared by the renderer, asset tools and gameplay. Scalar and
// unclamped: callers clamp where their inputs can leave [0, 1].
namespace Engine
{

	// sRGB EOTF (IEC 61966-2-1): display-encoded value to linear.
	inline float SrgbToLinear(float encoded)
	{
		return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
	}

	// Inverse of SrgbToLinear: linear value to display-encoded.
	inline float LinearToSrgb(float linear)
	{
		return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}

} // namespace Engine
