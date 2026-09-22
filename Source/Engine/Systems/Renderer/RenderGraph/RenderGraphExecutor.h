#pragma once
#include "Engine/Systems/Renderer/RenderGraph/CompiledRenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/GraphPassTiming.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutorDesc.h"
#include "Engine/Systems/Renderer/RHI/RhiReadbackArena.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

namespace Swim::Render
{
	// One submission in flight per executor. Execute waits for its predecessor
	// before resetting commands, queries, views or pooled allocations. This is the
	// correctness baseline, not an async/frame-overlap scheduler. Device and imported
	// resources must outlive the executor; do not mutate them while it is in flight.
	// The executor owns one upload and one readback arena. Staged graph buffers are
	// suballocated after the predecessor wait, upload writers run before recording,
	// uploads are flushed before submission and readbacks are bound to the
	// submission's own completion value. No staging bytes are reused in flight.
	class RenderGraphExecutor
	{
	  public:
		explicit RenderGraphExecutor(Rhi::Device& device, const RenderGraphExecutorDesc& desc = {});
		~RenderGraphExecutor();
		RenderGraphExecutor(const RenderGraphExecutor&) = delete;
		RenderGraphExecutor& operator=(const RenderGraphExecutor&) = delete;
		// Owns CommandLists and its timeline. External binary/timeline waits and
		// signals (including WSI acquisition/presentation) and a fence are forwarded.
		Rhi::TimelinePoint Execute(const CompiledRenderGraph& graph, const Rhi::SubmitDesc& synchronization = {});
		void Wait();
		void Trim(); // Wait, then release pooled resources and invalidate results.
		Rhi::Buffer& GetExported(GraphBuffer resource) const;
		Rhi::Texture& GetExported(GraphTexture resource) const;
		// Nonblocking readback access for the latest successful execution. NotReady
		// until its completion value; spans remain valid until the next Execute/Trim.
		Rhi::ReadbackStatus TryGetReadback(GraphBuffer resource, std::span<const std::byte>& data);
		// Destination must match the readback size; untouched unless Ready.
		Rhi::ReadbackStatus TryReadback(GraphBuffer resource, std::span<std::byte> destination);
		std::uint64_t GetUploadCapacity() const;
		std::uint64_t GetReadbackCapacity() const;
		std::vector<GraphPassTiming> ReadTimings(); // Waits before reading query slots.
		std::size_t GetPooledResourceCount() const;

	  private:
		Rhi::RhiObject& GetExport(std::uint64_t graph, std::uint32_t index, GraphKind kind) const;
		void RecordBarrier(const GraphBarrier& barrier);
		void CreateInitialStaging();
		void StageBuffers(const CompiledRenderGraph& graph);
		const Rhi::ReadbackSlice& GetReadbackSlice(GraphBuffer resource) const;
		std::unique_ptr<Internal::GraphExecutionState> state;
	};
} // namespace Swim::Render
