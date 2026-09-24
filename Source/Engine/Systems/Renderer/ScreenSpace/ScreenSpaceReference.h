#pragma once
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceRecords.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceSettings.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Swim::Render
{
	// The camera the screen-space inputs were rendered with.
	struct ScreenSpaceView
	{
		std::array<float, 16> View{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // World -> view (rigid, right-handed, -Z forward).
		std::array<float, 16> Projection{};	 // View -> clip, row-major, unjittered perspective (w = -z), reverse-Z.
		std::array<float, 2> Jitter{ 0, 0 }; // NDC jitter of the frame (ForwardPlusView::Jitter).
	};

	// Packs the settings and view for a width x height frame. Throws std::invalid_argument
	// for invalid settings, a zero size, a non-finite or non-invertible view/projection,
	// a projection that is not perspective (row 3 must be 0, 0, -1, 0) or non-finite jitter.
	GpuScreenSpaceParams BuildScreenSpaceParams(const ScreenSpaceSettings& settings, const ScreenSpaceView& view, std::uint32_t width,
		std::uint32_t height, std::uint32_t noiseFrame);
} // namespace Swim::Render

namespace Swim::Render::ScreenSpace
{
	// The CPU definition of the screen-space effects (critical-path item 76).
	// Shaders/Slang/ScreenSpace mirrors every function, and the native smoke compares
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

		Plane(std::uint32_t width, std::uint32_t height, T value = {})
			: Width(width), Height(height), Texels(std::size_t(width) * height, value)
		{
		}

		T& At(std::uint32_t x, std::uint32_t y) { return Texels[std::size_t(y) * Width + x]; }

		const T& At(std::uint32_t x, std::uint32_t y) const { return Texels[std::size_t(y) * Width + x]; }
	};

	using ColorImage = Plane<Float4>;
	using ScalarImage = Plane<float>;

	// General 4x4 inverse (row-major); std::nullopt when singular or non-finite.
	std::optional<std::array<float, 16>> Inverse(const std::array<float, 16>& m);

	// Interleaved gradient noise in [0, 1) at a pixel, rotated by the frame (0 .. 63).
	float InterleavedGradientNoise(float x, float y, std::uint32_t frame);

	// View-space position of a pixel centre (px, py in pixels, top-left origin) with a
	// reverse-Z depth; std::nullopt for the sky (depth <= 0).
	std::optional<Float3> ViewPosition(const GpuScreenSpaceParams& params, float px, float py, float depth);
	// The world normal rotated into view space and normalized; std::nullopt for a zero normal.
	std::optional<Float3> ViewNormal(const GpuScreenSpaceParams& params, const Float3& worldNormal);

	// GTAO cosine-weighted visibility, >= 0 and not yet clamped (1 = unoccluded, and for
	// sky or normal-less pixels; single slices of open surfaces can exceed 1). Slice s of SliceCount runs along screen
	// direction (s + noise) * pi / SliceCount. Each side takes StepCount samples at
	// (k + noise2) / StepCount of the projected radius, snapped to pixel centres.
	// Horizons fade to the normal's hemisphere with distance (AoFalloff), are
	// clamped to it, and each slice integrates the visible arc weighted by the
	// projected normal length.
	float GtaoTexel(
		const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, std::uint32_t x, std::uint32_t y);
	ScalarImage Gtao(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal);

	// 5x5 depth-aware blur: weight max(0, 1 - |dz| / (tolerance * |z|)) in view depth; sky
	// neighbours are skipped and sky pixels keep their value. The result is clamped to
	// [0, 1] and raised to AoPower: the final visibility.
	float BlurTexel(const GpuScreenSpaceParams& params, const ScalarImage& ao, const ScalarImage& depth, std::uint32_t x, std::uint32_t y);
	ScalarImage Blur(const GpuScreenSpaceParams& params, const ScalarImage& ao, const ScalarImage& depth);

	// Fog along the camera ray `direction` (unit) out to `distance` from `camera`.
	struct FogSample
	{
		float Transmittance = 1.0f;
		Float3 Inscatter{ 0, 0, 0 }; // Radiance the fog adds at full opacity.
	};

	float FogOpticalDepth(const GpuScreenSpaceParams& params, const Float3& camera, const Float3& direction, float distance);
	float HenyeyGreenstein(float g, float cosTheta); // Normalized so that g = 0 gives 1.
	FogSample EvaluateFog(const GpuScreenSpaceParams& params, const Float3& camera, const Float3& direction, float distance);

	// One output texel: color - (1 - ao) * indirect (clamped at 0) when AO is on, then
	// color * T + inscatter * (1 - T) when fog is on. The sky (depth 0) is fogged at
	// FogMaxDistance. Alpha is kept.
	Float4 CompositeTexel(const GpuScreenSpaceParams& params, const Float4& color, const Float4& indirect, float ao, float depth,
		std::uint32_t x, std::uint32_t y);
	ColorImage Composite(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect, const ScalarImage* ao,
		const ScalarImage& depth);
} // namespace Swim::Render::ScreenSpace
