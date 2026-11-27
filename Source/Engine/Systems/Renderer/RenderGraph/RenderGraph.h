#pragma once
#include "Engine/Systems/Renderer/RenderGraph/CompiledRenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphBuilder.h"

#include <functional>
#include <vector>

namespace Swim::Render
{

	// Remembers the last compiled shape: a graph of the same shape (passes, their uses and
	// dependencies, resource kinds, sizes, formats and usages, import and export states)
	// reuses its schedule, barriers and allocations instead of compiling again, and lets the
	// executor keep its transient allocations and descriptor tables. A resize, a settings
	// toggle or a feature added or removed changes the shape and compiles once.
	class RenderGraphCompileCache
	{

	  public:

		std::uint64_t GetHits() const { return hits; }

		std::uint64_t GetMisses() const { return misses; }

	  private:

		friend class RenderGraph;

		// Recent shapes, most recently used last: frames alternate between a few graphs (an
		// upload present on some frames), and each keeps its compiled shape.
		struct Entry
		{
			std::vector<std::uint64_t> Key;
			std::shared_ptr<const Internal::GraphShape> Shape;
		};

		static constexpr std::size_t Capacity = 8;
		std::vector<Entry> entries;
		std::vector<std::uint64_t> scratch;
		std::uint64_t hits = 0;
		std::uint64_t misses = 0;

	};

	// Single graphics-queue scheduler: pass Type describes commands, not a dedicated
	// queue. Imports must already belong to that family (WSI sharing is RHI-owned).
	// Write initializes the entire declared range. Loading/preserving contents needs
	// ReadWrite. Buffer hazards are whole-buffer; textures track each mip/layer,
	// with depth and stencil coupled. Callbacks must obey declarations and may not
	// issue their own barriers or submissions. Captured external objects and imports
	// must outlive execution AND GPU completion.
	// Unexported imports return to their initial state. Initially undefined imports
	// that are used must have an explicit final export state. Re-executing a compiled
	// graph requires the caller to reestablish its declared import states.
	// Upload/readback buffers are suballocated from the executor's staging arenas,
	// scheduled by the same graph, and retired by the executor's completion value.
	class RenderGraph
	{

	  public:

		RenderGraph();

		RenderGraph(const RenderGraph&) = delete;

		RenderGraph& operator=(const RenderGraph&) = delete;

		GraphBuffer CreateBuffer(const Rhi::BufferDesc& desc);

		GraphTexture CreateTexture(const Rhi::TextureDesc& desc);

		GraphBuffer ImportBuffer(Rhi::Buffer& buffer, Rhi::ResourceState initial, Rhi::QueueType owner = Rhi::QueueType::Graphics);

		GraphTexture ImportTexture(Rhi::Texture& texture, Rhi::ResourceState initial, Rhi::QueueType owner = Rhi::QueueType::Graphics);

		// Host-written staging read by later passes. The writer runs at execution.
		GraphBuffer CreateUpload(const GraphUploadDesc& desc, GraphUploadWriter writer);

		// Convenience: copies bytes into the compiled definition at declaration.
		GraphBuffer CreateUpload(std::span<const std::byte> bytes, std::string_view name,
			Rhi::BufferUsage usage = Rhi::BufferUsage::TransferSource, std::uint64_t alignment = 4, std::uint64_t elementStride = 0);

		// Host-read staging, implicitly exported to HostRead.
		GraphBuffer CreateReadback(const GraphReadbackDesc& desc);

		const Rhi::BufferDesc& GetDesc(GraphBuffer resource) const;

		const Rhi::TextureDesc& GetDesc(GraphTexture resource) const;

		void Export(GraphBuffer resource, Rhi::ResourceState final);

		void Export(GraphTexture resource, Rhi::ResourceState final);

		GraphPass AddPass(std::string_view name, Rhi::QueueType type, const std::function<void(RenderGraphBuilder&)>& setup,
			std::function<void(RenderCommandContext&)> execute);

		// Also supports forward edges after both passes have been declared.
		void AddDependency(GraphPass pass, GraphPass prerequisite);

		CompiledRenderGraph Compile() const;

		// Compiles through the cache, moving the definition into the result (no copy): the
		// graph is empty afterwards and is not used again.
		CompiledRenderGraph Compile(RenderGraphCompileCache& cache);

		// Unique for every graph, unlike its address (a frame's graph often lands in the stack
		// slot of the last one): the key for caching handles imported into one graph.
		std::uint64_t GetId() const { return id; }

		// Runs if the graph is abandoned instead of executed (a frame that failed while it was
		// built or recorded). Importers that changed their own state while recording - a
		// placeholder fill or an upload counted as done, a history marked written - put it back
		// here, so the next graph imports what is really on the GPU.
		void OnAbandoned(std::function<void()> undo);

		// The owner's call when the graph will not execute: runs the OnAbandoned callbacks once,
		// newest first.
		void Abandon();

	  private:

		void ExportResource(std::uint64_t graph, std::uint32_t index, GraphKind kind, Rhi::ResourceState final);

		Internal::GraphDefinition definition;
		std::uint64_t id = 0; // definition.Id, kept when Compile moves the definition out.
		std::vector<std::function<void()>> abandoned;
	};

} // namespace Swim::Render
