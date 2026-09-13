#pragma once
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphResource.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphPassDefinition.h"

namespace Swim::Render::Internal
{
	struct GraphDefinition
	{
		std::uint64_t Id = 0;
		std::vector<GraphResource> Resources;
		std::vector<GraphPassDefinition> Passes;
	};
} // namespace Swim::Render::Internal
