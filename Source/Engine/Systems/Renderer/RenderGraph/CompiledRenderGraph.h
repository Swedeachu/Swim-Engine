#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphScheduledPass.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphResourceLifetime.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphDefinition.h"

namespace Swim::Render
{
	class CompiledRenderGraph
	{
	  public:
		std::span<const GraphScheduledPass> GetSchedule() const { return schedule; }

		std::span<const GraphResourceLifetime> GetLifetimes() const { return lifetimes; }

		std::span<const GraphBarrier> GetFinalBarriers() const { return finalBarriers; }

		std::uint32_t GetAllocationCount() const { return static_cast<std::uint32_t>(allocations.size()); }

		std::string Dump() const;

	  private:
		friend class RenderGraph;
		friend class RenderGraphExecutor;
		friend class RenderCommandContext;
		std::shared_ptr<const Internal::GraphDefinition> definition;
		std::vector<GraphScheduledPass> schedule;
		std::vector<GraphResourceLifetime> lifetimes;
		std::vector<GraphBarrier> finalBarriers;
		std::vector<std::uint32_t> allocations;
		std::vector<std::vector<std::uint32_t>> dependencies;
	};
} // namespace Swim::Render
