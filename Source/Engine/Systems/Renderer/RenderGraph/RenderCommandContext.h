#pragma once
#include "Engine/Systems/Renderer/RenderGraph/GraphHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

namespace Swim::Render::Internal
{
	struct GraphExecutionState;
}

namespace Swim::Render
{
	class RenderCommandContext
	{
	  public:
		RenderCommandContext(const RenderCommandContext&) = delete;
		RenderCommandContext& operator=(const RenderCommandContext&) = delete;
		Rhi::CommandList& Commands() const;
		Rhi::Device& Device() const;
		Rhi::Buffer& Get(GraphBuffer resource) const;
		Rhi::Texture& Get(GraphTexture resource, Rhi::TextureSubresourceRange range = {}) const;
		Rhi::TextureView& CreateView(GraphTexture resource, const Rhi::TextureViewDesc& desc = {});
		// Descriptors/views created during recording must survive GPU execution.
		// Returned references are valid until the executor's next Execute/Trim.
		Rhi::RhiObject& Retain(std::unique_ptr<Rhi::RhiObject> object);

	  private:
		friend class RenderGraphExecutor;

		RenderCommandContext(Internal::GraphExecutionState& state, std::uint32_t pass) : state(state), pass(pass) {}

		Rhi::RhiObject& GetResource(std::uint64_t graph, std::uint32_t index, GraphKind kind, Rhi::TextureSubresourceRange range) const;
		Internal::GraphExecutionState& state;
		std::uint32_t pass;
	};
} // namespace Swim::Render
