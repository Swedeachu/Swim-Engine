#include "Engine/Systems/Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include <algorithm>

namespace Swim::Render
{
	void RenderGraphBuilder::Use(std::uint64_t graph, std::uint32_t index, GraphKind kind, GraphAccess access, Rhi::ResourceState state,
		Rhi::TextureSubresourceRange range)
	{
		const auto& r = Internal::RequireResource(definition, graph, index, kind);
		Internal::ValidateAccess(r, access, state, pass.Type);
		if (r.Staging == Internal::GraphStaging::Upload && access != GraphAccess::Read)
		{
			throw std::invalid_argument("RenderGraph upload buffers are GPU read-only: " + r.Name);
		}

		range = Internal::NormalizeRange(r, range);
		const auto cells = Internal::Cells(r, range);

		for (const auto& use : pass.Uses)
		{
			if (use.Resource != index)
			{
				continue;
			}
			for (auto cell : Internal::Cells(r, use.Range))
			{
				if (std::find(cells.begin(), cells.end(), cell) != cells.end())
				{
					throw std::invalid_argument("Overlapping RenderGraph declarations; use one ReadWrite declaration: " + r.Name);
				}
			}
		}

		pass.Uses.push_back({ index, access, state, range });
	}

	void RenderGraphBuilder::Read(GraphBuffer r, Rhi::ResourceState s)
	{
		Use(r.Graph, r.Index, GraphKind::Buffer, GraphAccess::Read, s, {});
	}

	void RenderGraphBuilder::Write(GraphBuffer r, Rhi::ResourceState s)
	{
		Use(r.Graph, r.Index, GraphKind::Buffer, GraphAccess::Write, s, {});
	}

	void RenderGraphBuilder::ReadWrite(GraphBuffer r, Rhi::ResourceState s)
	{
		Use(r.Graph, r.Index, GraphKind::Buffer, GraphAccess::ReadWrite, s, {});
	}

	void RenderGraphBuilder::Read(GraphTexture r, Rhi::ResourceState s, Rhi::TextureSubresourceRange range)
	{
		Use(r.Graph, r.Index, GraphKind::Texture, GraphAccess::Read, s, range);
	}

	void RenderGraphBuilder::Write(GraphTexture r, Rhi::ResourceState s, Rhi::TextureSubresourceRange range)
	{
		Use(r.Graph, r.Index, GraphKind::Texture, GraphAccess::Write, s, range);
	}

	void RenderGraphBuilder::ReadWrite(GraphTexture r, Rhi::ResourceState s, Rhi::TextureSubresourceRange range)
	{
		Use(r.Graph, r.Index, GraphKind::Texture, GraphAccess::ReadWrite, s, range);
	}

	void RenderGraphBuilder::DependsOn(GraphPass dependency)
	{
		if (dependency.Graph != definition.Id || dependency.Index >= definition.Passes.size())
		{
			throw std::invalid_argument("Invalid RenderGraph pass dependency");
		}
		pass.Dependencies.push_back(dependency.Index);
	}

	void RenderGraphBuilder::SideEffect()
	{
		pass.SideEffect = true;
	}
} // namespace Swim::Render
