#pragma once
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphDefinition.h"

namespace Swim::Render::Internal
{

	void ValidateName(std::string_view name);
	const GraphResource& RequireResource(const GraphDefinition& graph, std::uint64_t owner, std::uint32_t index, GraphKind kind);
	void ValidateState(const GraphResource& resource, Rhi::ResourceState state, bool allowUndefined = false);
	void ValidateAccess(const GraphResource& resource, GraphAccess access, Rhi::ResourceState state, Rhi::QueueType type);
	Rhi::TextureSubresourceRange NormalizeRange(const GraphResource& resource, Rhi::TextureSubresourceRange range);
	std::vector<std::uint32_t> Cells(const GraphResource& resource, const Rhi::TextureSubresourceRange& range);

	template <typename Visit> void ForEachCell(const GraphResource& resource, const Rhi::TextureSubresourceRange& range, Visit&& visit)
	{
		if (resource.Kind == GraphKind::Buffer)
		{
			visit(0u);
			return;
		}

		for (std::uint32_t layer = range.BaseArrayLayer; layer < range.BaseArrayLayer + range.ArrayLayerCount; ++layer)
		{
			for (std::uint32_t mip = range.BaseMipLevel; mip < range.BaseMipLevel + range.MipLevelCount; ++mip)
			{
				visit(layer * resource.Texture.MipLevels + mip);
			}
		}
	}

	std::uint32_t CellCount(const GraphResource& resource);
	// Pooled buffers are allocated in quarter steps between powers of two, so sizes that follow
	// per-frame counts keep the compiled shape and the pooled allocation.
	std::uint64_t PooledBufferSize(std::uint64_t size);
	bool Compatible(const GraphResource& left, const GraphResource& right);

} // namespace Swim::Render::Internal
