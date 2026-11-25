#pragma once
#include "Engine/Systems/Renderer/RenderGraph/CompiledRenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphBufferRange.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutorDesc.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphPooledResource.h"
#include "Engine/Systems/Renderer/RHI/RhiReadbackArena.h"
#include "Engine/Systems/Renderer/RHI/RhiUploadArena.h"

#include <unordered_map>

namespace Swim::Render::Internal
{

	// A descriptor table kept for the same pass and creation order of the next graph of the
	// same shape (RenderCommandContext::CreateDescriptorTable).
	struct GraphCachedTable
	{
		const Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
		std::unique_ptr<Rhi::DescriptorTable> Table;
	};

	struct GraphIssuedTable
	{
		std::uint64_t Key = 0; // Pass << 32 | creation order within the pass.
		const Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
		const Rhi::RhiObject* Table = nullptr;
	};

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
		std::shared_ptr<const GraphShape> PoolShape;	// The shape Pool was allocated for (in allocation order).
		std::unordered_map<std::uint64_t, GraphCachedTable> Tables;
		std::vector<GraphIssuedTable> IssuedTables; // Created through the context in the current execution.
		std::shared_ptr<const GraphShape> TablesShape;
		std::uint64_t TablesLayoutGeneration = UINT64_MAX;
		std::uint64_t Submitted = 0;
		bool Recording = false;
		bool HasResult = false;
	};

} // namespace Swim::Render::Internal
