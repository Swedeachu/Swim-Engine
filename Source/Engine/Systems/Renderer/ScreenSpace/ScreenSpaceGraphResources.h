#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceRecords.h"

#include <optional>

namespace Swim::Render
{
	// What one ScreenSpaceEffects::Record scheduled.
	struct ScreenSpaceGraphResources
	{
		GraphTexture Output;							 // RGBA16Float result; the input Color itself when every effect is off.
		std::optional<GraphTexture> AmbientOcclusionRaw; // R32Float GTAO visibility (AO on).
		std::optional<GraphTexture> AmbientOcclusion;	 // R32Float blurred visibility (AO on).
		std::optional<GraphTexture> Reflection;			 // RGBA16Float hit radiance + confidence (SSR on).
		std::optional<GraphBuffer> Params;
		std::optional<GraphPass> AmbientOcclusionPass;
		std::optional<GraphPass> BlurPass;
		std::optional<GraphPass> ReflectionPass;
		std::optional<GraphPass> CompositePass;
		GpuScreenSpaceParams ParamsRecord;
		bool Passthrough = false; // AO, reflections and fog all off: nothing was recorded.
	};
} // namespace Swim::Render
