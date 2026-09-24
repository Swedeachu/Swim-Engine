#pragma once
#include "Engine/Systems/Renderer/PostProcess/PostProcessRecords.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Swim::Render
{
	// What one PostProcessor::Record scheduled.
	struct PostProcessGraphResources
	{
		std::optional<GraphBuffer> Histogram; // 256 uint; automatic exposure only.
		GraphBuffer ExposureState;			  // The imported persistent GpuExposureState.
		GraphBuffer Params;					  // One GpuPostParams.
		std::vector<GraphTexture> BloomDown;  // RGBA16Float levels 0..n-1 (level i is (w >> (i + 1)) x (h >> (i + 1))).
		std::vector<GraphTexture> BloomUp;	  // Accumulated levels 0..n-2 (level n-1 is BloomDown.back()).
		std::uint32_t BloomLevels = 0;
		GpuPostParams ParamsRecord;
		bool ExposureReset = false; // This frame snapped instead of adapting.
		GraphPass ExposurePass;
		GraphPass CompositePass;
	};
} // namespace Swim::Render
