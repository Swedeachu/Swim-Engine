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

		// Declared by one of the pass's uses of this resource: usually a single use contains
		// the whole requested range (checked on the mip/layer intervals); otherwise every
		// requested cell must be covered by some use. (Enumerating cells per use was O(n^2)
		// on large arrays - a probe atlas has hundreds of subresources.)
		const auto& uses = definition.Passes[pass].Uses;
		for (const auto& use : uses)
		{
			if (use.Resource != index)
			{
				continue;
			}
			if (r.Kind == GraphKind::Buffer)
			{
				return *state.Resources[index];
			}
			const auto u = Internal::NormalizeRange(r, use.Range);
			if (range.BaseMipLevel >= u.BaseMipLevel && range.BaseMipLevel + range.MipLevelCount <= u.BaseMipLevel + u.MipLevelCount &&
				range.BaseArrayLayer >= u.BaseArrayLayer && range.BaseArrayLayer + range.ArrayLayerCount <= u.BaseArrayLayer + u.ArrayLayerCount)
			{
				return *state.Resources[index];
			}
		}
		std::vector<bool> covered(Internal::CellCount(r), false);
		for (const auto& use : uses)
		{
			if (use.Resource == index)
			{
				for (const auto cell : Internal::Cells(r, Internal::NormalizeRange(r, use.Range)))
				{
					covered[cell] = true;
				}
			}
		}
		for (const auto cell : Internal::Cells(r, range))
		{
			if (!covered[cell])
			{
				throw std::invalid_argument("RenderGraph callback accesses an undeclared subresource: " + r.Name);
			}
		}

		return *state.Resources[index];
	}

	Rhi::Buffer& RenderCommandContext::Get(GraphBuffer r) const
	{
		auto& buffer = static_cast<Rhi::Buffer&>(GetResource(r.Graph, r.Index, GraphKind::Buffer, {}));
		if (state.Graph.definition->Resources[r.Index].Staging != Internal::GraphStaging::None)
		{
			throw std::invalid_argument(
				"Staged RenderGraph buffers are suballocated; use GetRange: " + state.Graph.definition->Resources[r.Index].Name);
		}
		return buffer;
	}

	GraphBufferRange RenderCommandContext::GetRange(GraphBuffer r) const
	{
		auto& buffer = static_cast<Rhi::Buffer&>(GetResource(r.Graph, r.Index, GraphKind::Buffer, {}));
		if (state.Ranges[r.Index].Buffer)
		{
			return state.Ranges[r.Index];
		}
		return { &buffer, 0, buffer.GetDesc().Size };
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
