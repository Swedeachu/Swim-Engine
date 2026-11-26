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

	// A descriptor table kept for the next request with the same layout and space, preferably
	// from the same pass and creation order (RenderCommandContext::CreateDescriptorTable).
	struct GraphCachedTable
	{
		std::uint64_t Key = 0;
		std::unique_ptr<Rhi::DescriptorTable> Table;
	};

	struct GraphTableSlot
	{
		const Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;

		bool operator==(const GraphTableSlot&) const = default;
	};

	struct GraphTableSlotHash
	{
		std::size_t operator()(const GraphTableSlot& slot) const
		{
			return std::hash<const void*>{}(slot.Layout) ^ (std::size_t(slot.Space) * 0x9E3779B97F4A7C15ull);
		}
	};

	struct GraphIssuedTable
	{
		std::uint64_t Key = 0; // Pass << 32 | creation order within the pass.
		const Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
		const Rhi::RhiObject* Table = nullptr;
	};

	// A view of a pooled transient, kept while the pool holds the same objects and the view
	// was used by the previous execution (RenderCommandContext::CreateView).
	struct GraphViewKey
	{
		const Rhi::RhiObject* Texture = nullptr;
		Rhi::TextureViewDimension Dimension{};
		Rhi::Format PixelFormat{};
		std::uint32_t BaseMipLevel = 0;
		std::uint32_t MipLevelCount = 0;
		std::uint32_t BaseArrayLayer = 0;
		std::uint32_t ArrayLayerCount = 0;
		Rhi::TextureAspect Aspect{};

		bool operator==(const GraphViewKey&) const = default;
	};

	struct GraphViewKeyHash
	{
		std::size_t operator()(const GraphViewKey& key) const
		{
			std::uint64_t h = reinterpret_cast<std::uintptr_t>(key.Texture);
			const std::uint32_t parts[] = { std::uint32_t(key.Dimension), std::uint32_t(key.PixelFormat), key.BaseMipLevel,
				key.MipLevelCount, key.BaseArrayLayer, key.ArrayLayerCount, std::uint32_t(key.Aspect) };

			for (const auto part : parts)
			{
				h = (h ^ part) * 0x100000001B3ull;
			}

			return static_cast<std::size_t>(h);
		}
	};

	struct GraphCachedView
	{
		std::unique_ptr<Rhi::TextureView> View;
		std::uint64_t Used = 0; // Execution that last used it.
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
		std::unordered_map<GraphTableSlot, std::vector<GraphCachedTable>, GraphTableSlotHash> Tables;
		std::vector<GraphIssuedTable> IssuedTables; // Created through the context in the current execution.
		std::shared_ptr<const GraphShape> TablesShape;
		std::uint64_t TablesLayoutGeneration = UINT64_MAX;
		std::unordered_map<GraphViewKey, GraphCachedView, GraphViewKeyHash> Views; // Destroyed before Pool.
		std::uint64_t Executions = 0;
		std::uint64_t Submitted = 0;
		bool Recording = false;
		bool HasResult = false;
	};

} // namespace Swim::Render::Internal
