#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphResourceLifetime.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphScheduledPass.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphDefinition.h"

namespace Swim::Render::Internal
{

	// What compiling derives from a graph's shape alone (passes, their uses and dependencies,
	// resource kinds, descriptions, import and export states), never from the objects or
	// callbacks: graphs with the same shape share it (RenderGraphCompileCache).
	struct GraphShape
	{
		std::vector<GraphScheduledPass> Schedule;
		std::vector<GraphResourceLifetime> Lifetimes;
		std::vector<GraphBarrier> FinalBarriers;
		std::vector<std::uint32_t> Allocations;
		std::vector<std::vector<std::uint32_t>> Dependencies;
	};

	std::shared_ptr<const GraphShape> BuildGraphShape(const GraphDefinition& definition);

	// Everything BuildGraphShape reads, flattened; equal keys give equal shapes.
	void AppendGraphShapeKey(const GraphDefinition& definition, std::vector<std::uint64_t>& key);

} // namespace Swim::Render::Internal
