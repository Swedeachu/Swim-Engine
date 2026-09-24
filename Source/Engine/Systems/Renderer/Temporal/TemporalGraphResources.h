#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"

#include <array>
#include <cstdint>

namespace Swim::Render
{
	// What one TemporalAntiAliasing::Record scheduled.
	struct TemporalGraphResources
	{
		GraphTexture Output;  // Imported history texture written this frame (exported ShaderRead): the anti-aliased image.
		GraphTexture History; // What the resolve read as history: last frame's output, or Color when HistoryValid is false.
		GraphPass ResolvePass;
		bool HistoryValid = false;
		std::array<float, 2> JitterPixels{ 0, 0 }; // The jitter this frame was expected to be rendered with.
		std::uint64_t FrameIndex = 0;			   // Frames recorded before this one since construction.
	};
} // namespace Swim::Render
