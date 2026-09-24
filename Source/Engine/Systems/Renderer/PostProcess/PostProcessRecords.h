#pragma once
#include <cstdint>

namespace Swim::Render
{
	// PostProcessRecords.slang's ExposureState (16 bytes): the persistent auto-exposure
	// state, written by the exposure pass every frame.
	struct GpuExposureState
	{
		float Ev100 = 0.0f;
		float AverageLog2Luminance = 0.0f; // Last measured average (or the fallback).
		float Exposure = 1.0f;			   // Multiplier applied to scene color: 1 / (1.2 * 2^Ev100).
		std::uint32_t Valid = 0;		   // 0 until the first frame: the next frame snaps.
	};

	static_assert(sizeof(GpuExposureState) == 16);

	// PostProcessRecords.slang's PostParams (std430, 128 bytes): the composite pass's
	// grading, tone-mapping and output state (Post::BuildPostParams).
	struct GpuPostParams
	{
		float WhiteBalance[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }; // Three rows (w unused), linear Rec.709.
		float SlopeContrast[4] = { 1, 1, 1, 1 };						 // xyz slope, w contrast.
		float OffsetSaturation[4] = { 0, 0, 0, 1 };						 // xyz offset, w saturation.
		float PowerBloom[4] = { 1, 1, 1, 0 };							 // xyz power, w bloom intensity / levels.
		std::uint32_t ToneMapper = 3;
		std::uint32_t Encoding = 0;
		std::uint32_t Dither = 0;
		std::uint32_t BloomEnabled = 0;
		float WhitePoint = 4.0f;
		float PaperWhiteNits = 200.0f;
		float PeakNits = 1000.0f;
		std::uint32_t GradingEnabled = 0; // 0: every grading step is the identity and is skipped.
	};

	static_assert(sizeof(GpuPostParams) == 128);

	// Push constants of each program (PostProcessBindings.h names the sizes).
	struct PostHistogramConstants
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		float MinLog2Luminance = 0.0f;
		float InverseLog2Range = 0.0f;
	};

	struct PostExposureConstants
	{
		std::uint32_t Mode = 0; // ExposureMode.
		float ManualEv100 = 0.0f;
		float Compensation = 0.0f;
		float MinEv100 = 0.0f;
		float MaxEv100 = 0.0f;
		float LowPercentile = 0.0f;
		float HighPercentile = 0.0f;
		float SpeedUp = 0.0f;
		float SpeedDown = 0.0f;
		float DeltaTime = 0.0f;
		float MinLog2Luminance = 0.0f;
		float Log2Range = 0.0f;
		std::uint32_t Reset = 0;
		std::uint32_t Reserved[3] = {};
	};

	struct PostBloomDownsampleConstants
	{
		std::uint32_t SourceWidth = 0;
		std::uint32_t SourceHeight = 0;
		std::uint32_t DestinationWidth = 0;
		std::uint32_t DestinationHeight = 0;
		std::uint32_t First = 0; // 1: exposure, Karis average and threshold.
		float Threshold = 0.0f;
		float Knee = 0.0f;
		std::uint32_t Reserved = 0;
	};

	struct PostBloomUpsampleConstants
	{
		std::uint32_t LowWidth = 0;
		std::uint32_t LowHeight = 0;
		std::uint32_t DestinationWidth = 0;
		std::uint32_t DestinationHeight = 0;
	};

	struct PostCompositeConstants
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t BloomWidth = 0;
		std::uint32_t BloomHeight = 0;
	};

	static_assert(sizeof(PostHistogramConstants) == 16 && sizeof(PostExposureConstants) == 64);
	static_assert(sizeof(PostBloomDownsampleConstants) == 32 && sizeof(PostBloomUpsampleConstants) == 16);
	static_assert(sizeof(PostCompositeConstants) == 16);
} // namespace Swim::Render
