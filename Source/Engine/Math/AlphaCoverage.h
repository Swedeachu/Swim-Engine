#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

// Alpha-tested textures lose coverage down their mips: averaged alpha drops under the cutoff,
// so leaves and grass thin out and vanish with distance. Scaling each mip's alpha until the
// share of texels passing the cutoff matches the top mip's keeps them filled in (Castano,
// "Computing Alpha Mipmaps"). Tightly packed RGBA8, alpha in the fourth byte.
namespace Engine
{

	inline std::uint8_t ScaledAlpha(std::byte alpha, float scale)
	{
		return static_cast<std::uint8_t>(std::min(255.0f, std::round(static_cast<float>(std::to_integer<std::uint8_t>(alpha)) * scale)));
	}

	// Share of texels whose alpha (times scale) passes cutoff (0..1).
	inline float AlphaCoverage(std::span<const std::byte> rgba, float cutoff, float scale = 1.0f)
	{
		const std::size_t texels = rgba.size() / 4;

		if (texels == 0)
		{
			return 0.0f;
		}

		const float threshold = cutoff * 255.0f;
		std::size_t passing = 0;

		for (std::size_t i = 0; i < texels; ++i)
		{
			passing += static_cast<float>(ScaledAlpha(rgba[i * 4 + 3], scale)) >= threshold ? 1u : 0u;
		}

		return static_cast<float>(passing) / static_cast<float>(texels);
	}

	// Scales the alpha so its coverage at cutoff comes as close to target as it can.
	inline void PreserveAlphaCoverage(std::span<std::byte> rgba, float cutoff, float target)
	{
		if (!(cutoff > 0.0f) || rgba.size() < 4)
		{
			return;
		}

		// Coverage depends only on each alpha value, so the search runs over a histogram.
		std::array<std::size_t, 256> histogram{};

		for (std::size_t i = 3; i < rgba.size(); i += 4)
		{
			++histogram[std::to_integer<std::uint8_t>(rgba[i])];
		}

		// Passing is monotonic in alpha: count from the lowest passing value (binary search).
		std::array<std::size_t, 257> atLeast{};

		for (int a = 255; a >= 0; --a)
		{
			atLeast[a] = atLeast[a + 1] + histogram[a];
		}

		const float threshold = cutoff * 255.0f;
		const float texels = static_cast<float>(rgba.size() / 4);
		const auto coverage = [&](float scale)
		{
			int first = 0;
			int last = 256; // The lowest passing alpha is in [first, last]; 256: none passes.

			while (first < last)
			{
				const int middle = (first + last) / 2;

				if (static_cast<float>(ScaledAlpha(std::byte(middle), scale)) >= threshold)
				{
					last = middle;
				}
				else
				{
					first = middle + 1;
				}
			}

			return static_cast<float>(atLeast[first]) / texels;
		};

		float low = 0.0f;
		float high = 16.0f;

		for (int i = 0; i < 16; ++i)
		{
			const float middle = 0.5f * (low + high);
			(coverage(middle) < target ? low : high) = middle;
		}

		const float scale = std::abs(coverage(low) - target) < std::abs(coverage(high) - target) ? low : high;

		for (std::size_t i = 3; i < rgba.size(); i += 4)
		{
			rgba[i] = std::byte{ ScaledAlpha(rgba[i], scale) };
		}
	}

} // namespace Engine
