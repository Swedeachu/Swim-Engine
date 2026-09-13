#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include <algorithm>
#include <atomic>

namespace Swim::Render
{
	RenderGraph::RenderGraph()
	{
		static std::atomic<std::uint64_t> next{ 1 };
		definition.Id = next.fetch_add(1, std::memory_order_relaxed);
		if (!definition.Id)
		{
			throw std::overflow_error("RenderGraph identity exhausted");
		}
	}

	GraphBuffer RenderGraph::CreateBuffer(const Rhi::BufferDesc& desc)
	{
		if (!desc.Size || desc.Usage == Rhi::BufferUsage::None)
		{
			throw std::invalid_argument("RenderGraph buffer needs size and usage");
		}

		Internal::GraphResource resource;
		resource.Name = desc.DebugName.empty() ? "GraphBuffer" : std::string(desc.DebugName);
		Internal::ValidateName(resource.Name);
		resource.Buffer = desc;
		resource.Buffer.DebugName = {};

		definition.Resources.push_back(std::move(resource));
		return { definition.Id, static_cast<std::uint32_t>(definition.Resources.size() - 1) };
	}

	GraphTexture RenderGraph::CreateTexture(const Rhi::TextureDesc& desc)
	{
		if (!desc.Extent.Width || !desc.Extent.Height || !desc.Extent.Depth || !desc.MipLevels || !desc.ArrayLayers ||
			desc.PixelFormat == Rhi::Format::Undefined || desc.Usage == Rhi::TextureUsage::None)
		{
			throw std::invalid_argument("RenderGraph texture needs extent, subresources, format and usage");
		}

		Internal::GraphResource resource;
		resource.Kind = GraphKind::Texture;
		resource.Name = desc.DebugName.empty() ? "GraphTexture" : std::string(desc.DebugName);
		Internal::ValidateName(resource.Name);
		resource.Texture = desc;
		resource.Texture.DebugName = {};
		Internal::CellCount(resource);

		definition.Resources.push_back(std::move(resource));
		return { definition.Id, static_cast<std::uint32_t>(definition.Resources.size() - 1) };
	}

	GraphBuffer RenderGraph::ImportBuffer(Rhi::Buffer& buffer, Rhi::ResourceState initial, Rhi::QueueType owner)
	{
		if (owner != Rhi::QueueType::Graphics)
		{
			throw std::invalid_argument(
				"RenderGraph imports require graphics-family ownership; async ownership transfers are not scheduled");
		}
		for (const auto& r : definition.Resources)
		{
			if (r.Imported == &buffer)
			{
				throw std::invalid_argument("Import an RHI object only once; reuse its graph handle");
			}
		}

		auto handle = CreateBuffer(buffer.GetDesc());
		auto& r = definition.Resources.back();
		r.Imported = &buffer;
		r.Initial = initial;
		try
		{
			Internal::ValidateState(r, initial, true);
		}
		catch (...)
		{
			definition.Resources.pop_back();
			throw;
		}
		return handle;
	}

	GraphTexture RenderGraph::ImportTexture(Rhi::Texture& texture, Rhi::ResourceState initial, Rhi::QueueType owner)
	{
		if (owner != Rhi::QueueType::Graphics)
		{
			throw std::invalid_argument(
				"RenderGraph imports require graphics-family ownership; async ownership transfers are not scheduled");
		}
		for (const auto& r : definition.Resources)
		{
			if (r.Imported == &texture)
			{
				throw std::invalid_argument("Import an RHI object only once; reuse its graph handle");
			}
		}

		auto handle = CreateTexture(texture.GetDesc());
		auto& r = definition.Resources.back();
		r.Imported = &texture;
		r.Initial = initial;
		try
		{
			Internal::ValidateState(r, initial, true);
		}
		catch (...)
		{
			definition.Resources.pop_back();
			throw;
		}
		return handle;
	}

	void RenderGraph::ExportResource(std::uint64_t graph, std::uint32_t index, GraphKind kind, Rhi::ResourceState final)
	{
		const auto& r = Internal::RequireResource(definition, graph, index, kind);
		Internal::ValidateState(r, final);
		if (r.Exported && r.Final != final)
		{
			throw std::invalid_argument("Conflicting RenderGraph export states");
		}

		definition.Resources[index].Exported = true;
		definition.Resources[index].Final = final;
	}

	void RenderGraph::Export(GraphBuffer r, Rhi::ResourceState final)
	{
		ExportResource(r.Graph, r.Index, GraphKind::Buffer, final);
	}

	void RenderGraph::Export(GraphTexture r, Rhi::ResourceState final)
	{
		ExportResource(r.Graph, r.Index, GraphKind::Texture, final);
	}

	GraphPass RenderGraph::AddPass(std::string_view name, Rhi::QueueType type, const std::function<void(RenderGraphBuilder&)>& setup,
		std::function<void(RenderCommandContext&)> execute)
	{
		Internal::ValidateName(name);
		if (!setup || !execute || (type != Rhi::QueueType::Graphics && type != Rhi::QueueType::Compute && type != Rhi::QueueType::Transfer))
		{
			throw std::invalid_argument("RenderGraph pass needs setup, execution and a supported type");
		}

		Internal::GraphPassDefinition pass;
		pass.Name = name;
		pass.Type = type;
		pass.Execute = std::move(execute);

		RenderGraphBuilder builder(definition, pass);
		setup(builder);

		definition.Passes.push_back(std::move(pass));
		return { definition.Id, static_cast<std::uint32_t>(definition.Passes.size() - 1) };
	}

	void RenderGraph::AddDependency(GraphPass pass, GraphPass prerequisite)
	{
		if (pass.Graph != definition.Id || prerequisite.Graph != definition.Id || pass.Index >= definition.Passes.size() ||
			prerequisite.Index >= definition.Passes.size())
		{
			throw std::invalid_argument("Invalid RenderGraph dependency handle");
		}
		definition.Passes[pass.Index].Dependencies.push_back(prerequisite.Index);
	}
} // namespace Swim::Render
