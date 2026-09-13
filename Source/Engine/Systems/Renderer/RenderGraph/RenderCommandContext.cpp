#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphExecutionState.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include <algorithm>

namespace Swim::Render
{
	Rhi::CommandList& RenderCommandContext::Commands() const
	{
		return *state.Commands;
	}

	Rhi::Device& RenderCommandContext::Device() const
	{
		return state.Device;
	}

	Rhi::RhiObject& RenderCommandContext::GetResource(
		std::uint64_t graph, std::uint32_t index, GraphKind kind, Rhi::TextureSubresourceRange range) const
	{
		const auto& definition = *state.Graph.definition;
		const auto& r = Internal::RequireResource(definition, graph, index, kind);
		range = Internal::NormalizeRange(r, range);

		for (auto cell : Internal::Cells(r, range))
		{
			bool declared = false;
			for (const auto& use : definition.Passes[pass].Uses)
			{
				if (use.Resource == index)
				{
					const auto cells = Internal::Cells(r, use.Range);
					if (std::find(cells.begin(), cells.end(), cell) != cells.end())
					{
						declared = true;
						break;
					}
				}
			}
			if (!declared)
			{
				throw std::invalid_argument("RenderGraph callback accesses an undeclared subresource: " + r.Name);
			}
		}

		return *state.Resources[index];
	}

	Rhi::Buffer& RenderCommandContext::Get(GraphBuffer r) const
	{
		return static_cast<Rhi::Buffer&>(GetResource(r.Graph, r.Index, GraphKind::Buffer, {}));
	}

	Rhi::Texture& RenderCommandContext::Get(GraphTexture r, Rhi::TextureSubresourceRange range) const
	{
		return static_cast<Rhi::Texture&>(GetResource(r.Graph, r.Index, GraphKind::Texture, range));
	}

	Rhi::TextureView& RenderCommandContext::CreateView(GraphTexture resource, const Rhi::TextureViewDesc& desc)
	{
		auto& texture = Get(resource, { desc.BaseMipLevel, desc.MipLevelCount, desc.BaseArrayLayer, desc.ArrayLayerCount });

		auto view = state.Device.CreateTextureView(texture, desc);
		if (!view)
		{
			throw std::runtime_error("RenderGraph texture view allocation failed");
		}

		return static_cast<Rhi::TextureView&>(Retain(std::move(view)));
	}

	Rhi::RhiObject& RenderCommandContext::Retain(std::unique_ptr<Rhi::RhiObject> object)
	{
		if (!object)
		{
			throw std::invalid_argument("Cannot retain a null RenderGraph object");
		}

		state.Retained.push_back(std::move(object));
		return *state.Retained.back();
	}
} // namespace Swim::Render
