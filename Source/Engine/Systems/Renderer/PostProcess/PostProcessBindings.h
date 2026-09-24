#pragma once
#include "Engine/Systems/Renderer/PostProcess/PostProcessRecords.h"

#include <cstdint>

namespace Swim::Render
{
	// Descriptor contracts of Shaders/Slang/PostProcess. Every program uses space 0.

	inline constexpr std::uint32_t PostHistogramBins = 256; // Bin 0 = black (ignored), bins 1..255 cover the log range.

	struct PostHistogramBindings // PostHistogram.slang
	{
		static constexpr std::uint32_t Source = 0;			 // Texture2D<float4>: HDR scene color.
		static constexpr std::uint32_t Histogram = 1;		 // RWStructuredBuffer<uint>[256], cleared before the pass.
		static constexpr std::uint32_t ThreadGroupSize = 16; // 16x16 threads, one per pixel.
		static constexpr std::uint32_t PushConstantBytes = sizeof(PostHistogramConstants);
	};

	struct PostExposureBindings // PostExposure.slang: one thread.
	{
		static constexpr std::uint32_t Histogram = 0; // StructuredBuffer<uint>[256].
		static constexpr std::uint32_t State = 1;	  // RWStructuredBuffer<ExposureState>[1], persistent.
		static constexpr std::uint32_t PushConstantBytes = sizeof(PostExposureConstants);
	};

	struct PostBloomDownsampleBindings // PostBloomDownsample.slang
	{
		static constexpr std::uint32_t Source = 0;		// Texture2D<float4>: scene color or the previous level.
		static constexpr std::uint32_t Destination = 1; // RWTexture2D<float4> (rgba16f).
		static constexpr std::uint32_t State = 2;		// StructuredBuffer<ExposureState>: the first level's exposure.
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = sizeof(PostBloomDownsampleConstants);
	};

	struct PostBloomUpsampleBindings // PostBloomUpsample.slang
	{
		static constexpr std::uint32_t Low = 0;			// Texture2D<float4>: the smaller accumulated level.
		static constexpr std::uint32_t High = 1;		// Texture2D<float4>: the downsampled level of the destination size.
		static constexpr std::uint32_t Destination = 2; // RWTexture2D<float4> (rgba16f): High + tent(Low).
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = sizeof(PostBloomUpsampleConstants);
	};

	struct PostCompositeBindings // PostComposite.slang (SwimPostComposite rgba8, SwimPostCompositeHdr rgba16f)
	{
		static constexpr std::uint32_t Source = 0; // Texture2D<float4>: HDR scene color.
		static constexpr std::uint32_t Bloom = 1;  // Texture2D<float4>: accumulated half-resolution bloom (or a stand-in).
		static constexpr std::uint32_t State = 2;  // StructuredBuffer<ExposureState>.
		static constexpr std::uint32_t Params = 3; // StructuredBuffer<PostParams>[1].
		static constexpr std::uint32_t Output = 4; // RWTexture2D<float4>: rgba8 (SDR) or rgba16f (HDR).
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = sizeof(PostCompositeConstants);
	};
} // namespace Swim::Render
