#pragma once
#include "Engine/Systems/Renderer/RenderGraph/CompiledRenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphBufferRange.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutorDesc.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphPooledResource.h"
#include "Engine/Systems/Renderer/RHI/RhiReadbackArena.h"
#include "Engine/Systems/Renderer/RHI/RhiUploadArena.h"

namespace Swim::Render::Internal
{
	struct GraphExecutionState
	{
		GraphExecutionState(Rhi::Device& device, const RenderGraphExecutorDesc& desc) : Device(device), Desc(desc) {}

		Rhi::Device& Device;
		RenderGraphExecutorDesc Desc;
		CompiledRenderGraph Graph;
		// Shared so committed readback batches can observe completion after the
		// executor is destroyed, matching FrameContextRing's readback contract.
		std::shared_ptr<Rhi::Timeline> Timeline;
		std::unique_ptr<Rhi::CommandPool> CommandPool;
		std::unique_ptr<Rhi::CommandList> Commands;
		std::unique_ptr<Rhi::QueryPool> Queries;
		std::vector<GraphPooledResource> Pool;
		std::vector<Rhi::RhiObject*> Resources;
		std::vector<std::unique_ptr<Rhi::RhiObject>> Retained;
		std::unique_ptr<Rhi::UploadArena> Upload;
		std::unique_ptr<Rhi::ReadbackArena> Readback;
		std::vector<GraphBufferRange> Ranges;			// Staged suballocations; Buffer null otherwise.
		std::vector<Rhi::ReadbackSlice> ReadbackSlices; // Indexed by resource.
		std::uint64_t Submitted = 0;
		bool Recording = false;
		bool HasResult = false;
	};
} // namespace Swim::Render::Internal
