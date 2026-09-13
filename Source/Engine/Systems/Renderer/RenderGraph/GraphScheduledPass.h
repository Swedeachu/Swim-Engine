#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphBarrier.h"

namespace Swim::Render
{
	struct GraphScheduledPass
	{
		std::uint32_t Pass = 0;
		std::vector<GraphBarrier> Barriers;
	};
} // namespace Swim::Render
