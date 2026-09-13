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
	std::uint32_t CellCount(const GraphResource& resource);
	bool Compatible(const GraphResource& left, const GraphResource& right);
} // namespace Swim::Render::Internal
