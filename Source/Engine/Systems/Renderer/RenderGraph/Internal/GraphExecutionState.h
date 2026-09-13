#pragma once
#include "Engine/Systems/Renderer/RenderGraph/CompiledRenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphPooledResource.h"

namespace Swim::Render::Internal
{
	struct GraphExecutionState
	{
		explicit GraphExecutionState(Rhi::Device& device) : Device(device) {}

		Rhi::Device& Device;
		CompiledRenderGraph Graph;
		std::unique_ptr<Rhi::Timeline> Timeline;
		std::unique_ptr<Rhi::CommandPool> CommandPool;
		std::unique_ptr<Rhi::CommandList> Commands;
		std::unique_ptr<Rhi::QueryPool> Queries;
		std::vector<GraphPooledResource> Pool;
		std::vector<Rhi::RhiObject*> Resources;
		std::vector<std::unique_ptr<Rhi::RhiObject>> Retained;
		std::uint64_t Submitted = 0;
		bool Recording = false;
		bool HasResult = false;
	};
} // namespace Swim::Render::Internal
