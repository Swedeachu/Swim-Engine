#pragma once
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphUse.h"
#include <functional>

namespace Swim::Render
{
	class RenderCommandContext;
}

namespace Swim::Render::Internal
{
	struct GraphPassDefinition
	{
		std::string Name;
		Rhi::QueueType Type = Rhi::QueueType::Graphics;
		bool SideEffect = false;
		std::vector<GraphUse> Uses;
		std::vector<std::uint32_t> Dependencies;
		std::function<void(RenderCommandContext&)> Execute;
	};
} // namespace Swim::Render::Internal
