#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render
{
	// Post-processing controls (critical-path items 73-74). Every effect can be turned
	// off: manual exposure skips the histogram, disabled bloom records no bloom passes,
	// and default grading values are the identity.

	enum class ExposureMode : std::uint32_t
	{
		Manual = 0,	   // ManualEv100 - Compensation.
		Automatic = 1, // Luminance histogram -> average -> EV100, adapted over time.
	};

	struct ExposureSettings
	{
		ExposureMode Mode = ExposureMode::Automatic;
		float ManualEv100 = 0.0f;
		float Compensation = 0.0f; // EV: positive brightens.
		float MinEv100 = -6.0f;	   // Clamp of the resulting EV100.
		float MaxEv100 = 18.0f;
		// Histogram range of log2 scene luminance; darker pixels fall in the black bin
		// (ignored), brighter ones in the last bin.
		float MinLog2Luminance = -12.0f;
		float MaxLog2Luminance = 12.0f;
		// The average ignores the darkest LowPercentile and the brightest 1 - HighPercentile
		// of the (non-black) pixels.
		float LowPercentile = 0.5f;
		float HighPercentile = 0.95f;
		// Adaptation rates (1 / seconds) toward a brighter (higher EV) or darker scene.
		float SpeedUp = 3.0f;
		float SpeedDown = 1.0f;
	};

	enum class ToneMapper : std::uint32_t
	{
		Clamp = 0,		// Saturate only (debugging).
		Reinhard = 1,	// Per-channel extended Reinhard with a white point.
		Aces = 2,		// ACES RRT + ODT, Stephen Hill's fit.
		PbrNeutral = 3, // Khronos PBR Neutral: hue-preserving, linear below 0.76.
	};

	struct ToneMapSettings
	{
		ToneMapper Operator = ToneMapper::PbrNeutral;
		float WhitePoint = 4.0f; // Reinhard: the input that maps to 1 (>= 1).
	};

	struct BloomSettings
	{
		bool Enabled = true;
		std::uint32_t MipCount = 6; // Half-resolution and smaller levels (1..MaxBloomMips, fewer on small images).
		float Threshold = 1.0f;		// Exposed brightness where bloom starts (soft knee below it).
		float Knee = 0.5f;			// Width of the soft transition, > 0.
		float Intensity = 0.04f;	// Blend weight of the averaged bloom levels.
	};

	inline constexpr std::uint32_t MaxBloomMips = 8;

	// Applied in linear Rec.709 after exposure and bloom, before tone mapping:
	// white balance -> contrast -> ASC CDL (slope, offset, power) -> saturation.
	struct ColorGradingSettings
	{
		float Temperature = 0.0f; // [-100, 100]: positive is warmer.
		float Tint = 0.0f;		  // [-100, 100]: positive is more magenta.
		float Contrast = 1.0f;	  // Around 18 % grey, > 0.
		std::array<float, 3> Slope{ 1, 1, 1 };
		std::array<float, 3> Offset{ 0, 0, 0 };
		std::array<float, 3> Power{ 1, 1, 1 }; // > 0.
		float Saturation = 1.0f;			   // 0 = grey, >= 0.
	};

	enum class OutputEncoding : std::uint32_t
	{
		Srgb = 0,  // SDR: sRGB OETF into an RGBA8Unorm target (optionally dithered).
		Hdr10 = 1, // BT.2020 primaries, SMPTE ST 2084 (PQ) into an RGBA16Float target.
		ScRgb = 2, // Linear BT.709, 1.0 = 80 nits, into an RGBA16Float target.
	};

	struct OutputSettings
	{
		OutputEncoding Encoding = OutputEncoding::Srgb;
		float PaperWhiteNits = 200.0f; // HDR: luminance of tone-mapped 1.0 (SDR white).
		float PeakNits = 1000.0f;	   // HDR: the display's peak; the tone mapper's white.
		bool Dither = true;			   // SDR: +-0.5 LSB interleaved-gradient noise against banding.
	};

	struct PostProcessSettings
	{
		ExposureSettings Exposure;
		BloomSettings Bloom;
		ColorGradingSettings Grading;
		ToneMapSettings ToneMap;
		OutputSettings Output;
	};

	// Throws std::invalid_argument for an out-of-range or non-finite setting.
	void ValidatePostProcessSettings(const PostProcessSettings& settings);

	inline bool IsHdrEncoding(OutputEncoding encoding)
	{
		return encoding != OutputEncoding::Srgb;
	}
} // namespace Swim::Render
