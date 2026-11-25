#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphScheduledPass.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphResourceLifetime.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphShape.h"

namespace Swim::Render
{

	class CompiledRenderGraph
	{

	  public:

		std::span<const GraphScheduledPass> GetSchedule() const { return shape->Schedule; }

		std::span<const GraphResourceLifetime> GetLifetimes() const { return shape->Lifetimes; }

		std::span<const GraphBarrier> GetFinalBarriers() const { return shape->FinalBarriers; }

		std::uint32_t GetAllocationCount() const { return static_cast<std::uint32_t>(shape->Allocations.size()); }

		// Whether both were compiled from graphs of the same shape through one
		// RenderGraphCompileCache (they share the schedule, barriers and allocations).
		bool SharesShape(const CompiledRenderGraph& other) const { return shape && shape == other.shape; }

		std::string Dump() const;

	  private:

		friend class RenderGraph;
		friend class RenderGraphExecutor;
		friend class RenderCommandContext;
		std::shared_ptr<const Internal::GraphDefinition> definition;
		std::shared_ptr<const Internal::GraphShape> shape;

	};

} // namespace Swim::Render
