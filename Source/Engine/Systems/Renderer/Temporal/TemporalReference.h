#pragma once
#include "Engine/Systems/Renderer/Temporal/TemporalSettings.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Swim::Render::Temporal
{
	// The CPU definition of temporal anti-aliasing (critical-path item 75).
	// Shaders/Slang/Temporal mirrors every function, and the native TAA smoke compares
	// the two texel by texel.
	using Float2 = std::array<float, 2>;
	using Float3 = std::array<float, 3>;
	using Float4 = std::array<float, 4>;

	template <typename T> struct Plane
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<T> Texels; // Row-major, top row first.

		Plane() = default;

		Plane(std::uint32_t width, std::uint32_t height) : Width(width), Height(height), Texels(std::size_t(width) * height) {}

		T& At(std::uint32_t x, std::uint32_t y) { return Texels[std::size_t(y) * Width + x]; }

		const T& At(std::uint32_t x, std::uint32_t y) const { return Texels[std::size_t(y) * Width + x]; }
	};

	using ColorImage = Plane<Float4>;
	using VelocityImage = Plane<Float2>;
	using DepthImage = Plane<float>;

	// Radical inverse of index (>= 1) in base (>= 2).
	float Halton(std::uint32_t index, std::uint32_t base);
	// Sub-pixel jitter of a frame in pixels, each in (-0.5, 0.5): Halton(2, 3) at
	// 1 + frameIndex % phases, minus 0.5. Zero when phases == 0.
	Float2 JitterPixels(std::uint64_t frameIndex, std::uint32_t phases);
	// The same offset in NDC for ForwardPlusView::Jitter: x right, y up.
	Float2 JitterNdc(std::uint64_t frameIndex, std::uint32_t phases, std::uint32_t width, std::uint32_t height);

	float Luminance(const Float3& rgb); // Rec. 709.
	Float3 RgbToYCoCg(const Float3& rgb);
	Float3 YCoCgToRgb(const Float3& ycocg);
	// Non-finite channels become 0, negative ones are clamped to 0.
	Float3 Sanitize(const Float4& color);

	// Moves value toward the box centre until it lies inside [minimum, maximum]
	// (clip, not clamp: the hue of the history is kept).
	Float3 ClipToBox(const Float3& value, const Float3& minimum, const Float3& maximum);

	// Clamp-to-edge bilinear filter at uv (texel centres at (i + 0.5) / size), from Loads.
	Float4 SampleBilinear(const ColorImage& image, const Float2& uv);

	struct Neighborhood
	{
		Float3 Center;	// Sanitized current color.
		Float3 Minimum; // Clipping box in YCoCg: the variance box intersected with min/max.
		Float3 Maximum;
		Float2 Velocity; // Of the nearest texel (largest reverse-Z depth) in the 3 x 3 neighborhood.
	};

	// The 3 x 3 neighborhood of (x, y) with clamped coordinates, visited row by row
	// (dy then dx from -1 to 1); the first nearest texel wins ties.
	Neighborhood Gather(const ColorImage& current, const DepthImage& depth, const VelocityImage& velocity, std::uint32_t x, std::uint32_t y,
		float clipGamma);

	// One output texel: reproject by the dilated velocity, bilinear history, clip in
	// YCoCg, then blend with Feedback using luminance weights 1 / (1 + L). Without
	// history, or when the previous position is off screen, the current color.
	// Alpha is 1.
	Float4 ResolveTexel(const ColorImage& current, const DepthImage& depth, const VelocityImage& velocity, const ColorImage* history,
		const TemporalSettings& settings, std::uint32_t x, std::uint32_t y);

	// The whole frame; history == nullptr means no valid history. All images must have
	// the same size (std::invalid_argument otherwise).
	ColorImage Resolve(const ColorImage& current, const DepthImage& depth, const VelocityImage& velocity, const ColorImage* history,
		const TemporalSettings& settings);
} // namespace Swim::Render::Temporal
