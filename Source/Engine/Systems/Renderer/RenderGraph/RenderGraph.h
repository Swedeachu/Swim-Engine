#pragma once
#include "Engine/Systems/Renderer/RenderGraph/CompiledRenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphBuilder.h"

namespace Swim::Render
{
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
			Rhi::BufferUsage usage = Rhi::BufferUsage::TransferSource, std::uint64_t alignment = 4);
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

	  private:
		void ExportResource(std::uint64_t graph, std::uint32_t index, GraphKind kind, Rhi::ResourceState final);
		Internal::GraphDefinition definition;
	};
} // namespace Swim::Render
