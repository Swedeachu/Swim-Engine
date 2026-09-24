#pragma once
#include "Engine/Systems/Renderer/PostProcess/PostProcessBindings.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessRecords.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessSettings.h"

#include <array>
#include <cstdint>
#include <vector>

// CPU definition of every post-processing pass (items 73-74).
// Shaders/Slang/PostProcess mirrors each function; the native post-process smoke
// compares the GPU's histogram, exposure state, bloom levels and output with it.
namespace Swim::Render::Post
{
	using Float3 = std::array<float, 3>;
	using Float4 = std::array<float, 4>;
	using Matrix3 = std::array<float, 9>; // Row-major.
	using Histogram = std::array<std::uint32_t, PostHistogramBins>;

	// Linear RGBA image, rows top to bottom.
	struct Image
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<Float4> Texels;

		Image() = default;

		Image(std::uint32_t width, std::uint32_t height) : Width(width), Height(height), Texels(std::size_t(width) * height) {}

		const Float4& At(std::uint32_t x, std::uint32_t y) const { return Texels[std::size_t(y) * Width + x]; }

		Float4& At(std::uint32_t x, std::uint32_t y) { return Texels[std::size_t(y) * Width + x]; }

		// Clamp-to-edge texel fetch.
		const Float4& Clamped(int x, int y) const;
	};

	// IEEE binary16 round-to-nearest-even (what an RGBA16Float store keeps).
	float RoundToHalf(float value);
	Float4 RoundToHalf(const Float4& value);

	// Rec.709 relative luminance.
	float Luminance(const Float3& rgb);

	// ---- Exposure (item 73) ----

	// Bin 0 holds black (below 2^MinLog2Luminance, or NaN); bins 1..255 split the log range.
	std::uint32_t HistogramBin(float luminance, float minLog2, float inverseLog2Range);
	Histogram BuildHistogram(const Image& source, const ExposureSettings& settings);
	// Log2 luminance at the center of bin (1..255).
	float BinLog2Luminance(std::uint32_t bin, float minLog2, float log2Range);
	// Average log2 luminance between the percentiles; false when no pixel counts.
	bool AverageLog2Luminance(const Histogram& histogram, const ExposureSettings& settings, float& average);
	// Reflected-light meter (K = 12.5, ISO 100): EV100 = log2(L * 100 / 12.5).
	float Ev100FromAverageLog2(float averageLog2);
	// Saturation-based exposure: 1 / (1.2 * 2^EV100) (the largest unclipped luminance maps to 1).
	float ExposureFromEv100(float ev100);
	// One exposure pass. `reset` (or an invalid previous state) snaps instead of adapting.
	GpuExposureState UpdateExposure(
		const Histogram& histogram, const ExposureSettings& settings, const GpuExposureState& previous, float deltaTime, bool reset);

	// ---- Bloom (item 74) ----

	// Levels actually recorded: min(requested, levels whose size stays >= 1x1); level i is (w >> (i + 1)) x (h >> (i + 1)).
	std::uint32_t BloomLevelCount(std::uint32_t width, std::uint32_t height, std::uint32_t requested);
	// Soft-knee threshold weight applied to an exposed color.
	Float3 BloomThreshold(const Float3& color, float threshold, float knee);
	// 13-tap downsample (Jimenez 2014) from exact 2x2 box averages; the first level
	// multiplies by the exposure, uses the Karis average and applies the threshold.
	// Stored as RGBA16Float (rounded), alpha 1.
	Image BloomDownsample(
		const Image& source, std::uint32_t width, std::uint32_t height, bool first, float exposure, float threshold, float knee);
	// 3x3 tent over bilinear taps of `low` at the destination texel's position (4x4 exact weights).
	Float3 TentUpsample(const Image& low, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);
	// High + tent(Low), stored as RGBA16Float.
	Image BloomUpsample(const Image& low, const Image& high);

	// ---- Grading, tone mapping, output ----

	// Linear Rec.709 von Kries adaptation in LMS (identity at temperature = tint = 0).
	Matrix3 WhiteBalanceMatrix(float temperature, float tint);
	GpuPostParams BuildPostParams(const PostProcessSettings& settings, std::uint32_t bloomLevels);
	// White balance, contrast, CDL and saturation (skipped when GradingEnabled is 0).
	Float3 Grade(const GpuPostParams& params, const Float3& color);

	Float3 ToneMapReinhard(const Float3& color, float whitePoint);
	Float3 ToneMapAces(const Float3& color);
	Float3 ToneMapPbrNeutral(const Float3& color);
	Float3 ToneMap(ToneMapper op, const Float3& color, float whitePoint);

	float SrgbOetf(float linear);
	float SrgbEotf(float encoded);
	// SMPTE ST 2084 inverse EOTF: luminance in nits -> [0, 1].
	float PqOetf(float nits);
	float PqEotf(float encoded);
	Float3 Rec709ToRec2020(const Float3& color);
	// Interleaved gradient noise in [0, 1) at an integer pixel.
	float InterleavedGradientNoise(std::uint32_t x, std::uint32_t y);

	// One output texel before storage quantization: exposure, bloom, grading, tone
	// mapping and the output encoding (with dither for SDR).
	Float4 CompositeTexel(
		const GpuPostParams& params, float exposure, const Float4& source, const Float3& bloom, std::uint32_t x, std::uint32_t y);

	struct PostResult
	{
		Histogram Bins{};
		GpuExposureState State;
		std::vector<Image> Down; // Bloom levels 0..n-1.
		std::vector<Image> Up;	 // Accumulated levels; Up[n-1] is Down[n-1].
		Image Output;			 // As stored: SDR quantized to k / 255, HDR rounded to half.
		GpuPostParams Params;
	};

	// The whole frame: histogram (automatic exposure), exposure, bloom, composite.
	PostResult RunPostProcess(
		const Image& source, const PostProcessSettings& settings, const GpuExposureState& previous, float deltaTime, bool reset);
} // namespace Swim::Render::Post
