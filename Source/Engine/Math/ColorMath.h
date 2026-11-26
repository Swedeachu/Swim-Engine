#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

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

	// 8-bit sRGB code to linear, exact per code.
	inline const std::array<float, 256>& Srgb8ToLinearTable()
	{
		static const std::array<float, 256> table = []
		{
			std::array<float, 256> t{};

			for (int i = 0; i < 256; ++i)
			{
				t[i] = SrgbToLinear(static_cast<float>(i) / 255.0f);
			}

			return t;
		}();
		return table;
	}

	// Linear [0, 1] to the nearest 8-bit sRGB code through a table (1/65535 steps, finer than
	// one code everywhere), for per-texel work where pow is too slow. Clamps.
	inline std::uint8_t LinearToSrgb8(float linear)
	{
		constexpr std::uint32_t steps = 65535;
		static const std::vector<std::uint8_t> table = []
		{
			std::vector<std::uint8_t> t(steps + 1);

			for (std::uint32_t i = 0; i <= steps; ++i)
			{
				t[i] = static_cast<std::uint8_t>(std::clamp(LinearToSrgb(static_cast<float>(i) / steps), 0.0f, 1.0f) * 255.0f + 0.5f);
			}

			return t;
		}();
		return table[static_cast<std::uint32_t>(std::clamp(linear, 0.0f, 1.0f) * steps + 0.5f)];
	}

} // namespace Engine
