#include "Engine/Systems/Renderer/Shadows/ShadowRenderer.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;

		Rhi::DescriptorWrite BufferWrite(RenderCommandContext& c, std::uint32_t binding, GraphBuffer buffer)
		{
			const auto range = c.GetRange(buffer);
			Rhi::DescriptorWrite write{};
			write.Binding = binding;
			write.BufferResource = range.Buffer;
			write.BufferOffset = range.Offset;
			write.BufferRange = range.Size;
			return write;
		}

		Rhi::ShaderStageMask PushStages(const Rhi::PipelineLayout& layout)
		{
			const auto& ranges = layout.GetInterface().PushConstants;
			return ranges.empty() ? Rhi::ShaderStageMask::Vertex : ranges.front().Stages;
		}
	} // namespace

	Rhi::GraphicsPipelineDesc ShadowRenderer::PipelineDesc(Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout)
	{
		Rhi::GraphicsPipelineDesc pipeline{};
		pipeline.Program = &program;
		pipeline.Layout = &layout;
		pipeline.DepthStencilFormat = AtlasFormat;
		pipeline.DepthStencil.DepthTest = true;
		pipeline.DepthStencil.DepthWrite = true;
		pipeline.DepthStencil.DepthCompare = DepthCompareOp(CanonicalDepthConvention);
		pipeline.Raster.Cull = Rhi::CullMode::None;
		pipeline.Raster.Winding = Rhi::FrontFace::CounterClockwise;
		pipeline.DebugName = "Shadow depth";
		return pipeline;
	}

	std::vector<std::uint32_t> ShadowRenderer::VisibilityBinCapacities(std::uint32_t opaque, std::uint32_t masked, std::uint32_t excluded)
	{
		if (!opaque || !masked || !excluded)
		{
			throw std::invalid_argument("Shadow visibility bins need nonzero capacities");
		}
		return { opaque, masked, excluded };
	}

	ShadowBin ShadowRenderer::MaterialBin(const StandardPbr::Parameters& parameters)
	{
		if ((parameters.Flags & StandardPbr::FlagAlphaBlend) != 0)
		{
			return ShadowBin::Excluded;
		}
		return (parameters.Flags & StandardPbr::FlagAlphaMask) != 0 ? ShadowBin::Masked : ShadowBin::Opaque;
	}

	void ShadowRenderer::RouteMaterial(GpuVisibility& visibility, std::uint32_t materialSet, const StandardPbr::Parameters& parameters)
	{
		visibility.SetMaterialBin(materialSet, static_cast<std::uint32_t>(MaterialBin(parameters)));
	}

	ShadowRenderer::ShadowRenderer(ShadowRendererDesc descInput) : desc(std::move(descInput))
	{
		if (!desc.Opaque.Pipeline || !desc.Opaque.Layout || !desc.Masked.Pipeline || !desc.Masked.Layout)
		{
			throw std::invalid_argument(desc.DebugName + " needs the opaque and masked depth programs");
		}
	}

	ShadowGraphResources ShadowRenderer::Record(RenderGraph& graph, const ShadowFrame& frame) const
	{
		const auto& name = desc.DebugName;
		if (!frame.Scene || !frame.Geometry || !frame.Visibility || !frame.Materials || !frame.Bindless || !frame.Plan ||
			frame.PageSlots.empty() || frame.Plan->Records.empty() || frame.Plan->AtlasSize == 0)
		{
			throw std::invalid_argument(
				name + " frame needs the scene, geometry, visibility, materials, bindless table, page slots and a plan");
		}
		const auto& bins = frame.Visibility->GetBins();
		const auto slots = static_cast<std::uint32_t>(frame.PageSlots.size());
		if (bins.GetMaterialBins() != ShadowBinCount || bins.GetPageSlots() != slots)
		{
			throw std::invalid_argument(
				name + " needs a visibility instance with ShadowBinCount material bins over the frame's page slots");
		}
		std::vector<GraphBuffer> vertexPages;
		std::vector<GraphBuffer> indexPages;
		std::vector<std::uint32_t> indexPageIds;
		for (const auto& slot : frame.PageSlots)
		{
			if (slot.IndexPage >= frame.Geometry->Pages.size() || slot.VertexPage >= frame.Geometry->Pages.size() ||
				slot.IndexPage == slot.VertexPage)
			{
				throw std::invalid_argument(name + " page slot names a missing GeometryHeap page");
			}
			vertexPages.push_back(frame.Geometry->Pages[slot.VertexPage]);
			indexPages.push_back(frame.Geometry->Pages[slot.IndexPage]);
			indexPageIds.push_back(slot.IndexPage);
		}
		const auto& plan = *frame.Plan;

		ShadowGraphResources resources;
		resources.AtlasSize = plan.AtlasSize;
		resources.ViewCount = static_cast<std::uint32_t>(plan.Views.size());
		resources.RecordCount = static_cast<std::uint32_t>(plan.Records.size());
		resources.Records = graph.CreateUpload(std::as_bytes(std::span(plan.Records)), name + " records", Rhi::BufferUsage::Storage, 16);
		const std::vector<GpuShadowView> views = plan.Views.empty() ? std::vector<GpuShadowView>(1) : plan.Views;
		resources.Views = graph.CreateUpload(std::as_bytes(std::span(views)), name + " views", Rhi::BufferUsage::Storage, 16);
		Rhi::TextureDesc atlas;
		atlas.Extent = { plan.AtlasSize, plan.AtlasSize, 1 };
		atlas.PixelFormat = AtlasFormat;
		atlas.Usage = Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferSource;
		const std::string atlasName = name + " atlas";
		atlas.DebugName = atlasName;
		resources.Atlas = graph.CreateTexture(atlas);

		// GPU caster culling, one visibility record per view.
		for (const auto& draw : plan.Draws)
		{
			VisibilityFrameDesc visibilityFrame;
			visibilityFrame.View = BuildGpuViewRecord(draw.Visibility);
			visibilityFrame.IndexPages = indexPageIds;
			visibilityFrame.ReadStats = false;
			visibilityFrame.ZeroUnusedCommands = frame.ZeroUnusedCommands;
			resources.Visibility.push_back(frame.Visibility->Record(graph, *frame.Scene, *frame.Geometry, visibilityFrame));
		}

		struct DrawView
		{
			GraphBuffer Commands;
			GraphBuffer Counts;
			GraphBuffer DrawRecords;
			ShadowTile Tile;
		};

		std::vector<DrawView> drawViews;
		for (std::size_t v = 0; v < plan.Draws.size(); ++v)
		{
			drawViews.push_back({ resources.Visibility[v].Commands, resources.Visibility[v].Counts, resources.Visibility[v].DrawRecords,
				plan.Draws[v].Tile });
		}
		const auto instances = frame.Scene->Instances;
		const auto transforms = frame.Scene->Transforms;
		const auto materials = frame.Materials->Materials;
		const auto materialCount = frame.Materials->MaterialCount;
		const auto viewsBuffer = resources.Views;
		const auto atlasTexture = resources.Atlas;
		const auto atlasSize = plan.AtlasSize;
		resources.DepthPass = graph.AddPass(
			name + " depth", Rhi::QueueType::Graphics,
			[&](RenderGraphBuilder& b)
			{
				for (const auto& view : drawViews)
				{
					b.Read(view.Commands, S::IndirectArgument);
					b.Read(view.Counts, S::IndirectArgument);
					b.Read(view.DrawRecords, S::ShaderRead);
				}
				for (const auto buffer : { instances, transforms, viewsBuffer, materials })
				{
					b.Read(buffer, S::ShaderRead);
				}
				std::vector<GraphBuffer> declared;
				const auto once = [&](GraphBuffer page, S state)
				{
					if (std::find(declared.begin(), declared.end(), page) == declared.end())
					{
						declared.push_back(page);
						b.Read(page, state);
					}
				};
				for (const auto page : vertexPages)
				{
					once(page, S::ShaderRead);
				}
				for (const auto page : indexPages)
				{
					once(page, S::IndexBuffer);
				}
				b.Write(atlasTexture, S::DepthStencilWrite);
			},
			[programs = std::array<ShadowProgram, 2>{ desc.Opaque, desc.Masked }, label = name + " depth", drawViews, bins, vertexPages,
				indexPages, instances, transforms, viewsBuffer, materials, materialCount, atlasTexture, atlasSize, path = desc.DrawPath,
				bindless = frame.Bindless](RenderCommandContext& c)
			{
				Rhi::TextureViewDesc depthView;
				depthView.PixelFormat = AtlasFormat;
				const Rhi::DepthStencilAttachmentDesc depth{ &c.CreateView(atlasTexture, depthView), Rhi::LoadOp::Clear,
					Rhi::StoreOp::Store, DepthClearValue(CanonicalDepthConvention), 0 };
				auto& list = c.Commands();
				list.BeginRendering({ {}, &depth, { atlasSize, atlasSize } });
				for (std::uint32_t v = 0; v < drawViews.size(); ++v)
				{
					const auto& view = drawViews[v];
					const auto& tile = view.Tile;
					for (std::uint32_t variant = 0; variant < 2; ++variant)
					{
						const auto& program = programs[variant];
						list.BindGraphicsPipeline(*program.Pipeline);
						list.SetViewport({ float(tile.X), float(tile.Y), float(tile.Size), float(tile.Size), 0.0f, 1.0f });
						list.SetScissor({ std::int32_t(tile.X), std::int32_t(tile.Y), tile.Size, tile.Size });
						const std::array<std::uint32_t, 4> constants{ v, materialCount, 0, 0 };
						auto& commands = c.Get(view.Commands);
						auto& counts = c.Get(view.Counts);
						for (std::uint32_t slot = 0; slot < vertexPages.size(); ++slot)
						{
							auto table = c.Device().CreateDescriptorTable({ program.Layout, 0, 0, label });
							if (!table)
							{
								throw std::runtime_error(label + " descriptor table could not be created");
							}
							using B = ShadowDepthBindings;
							const std::array<Rhi::DescriptorWrite, B::Count> writes{ BufferWrite(c, B::Instances, instances),
								BufferWrite(c, B::Transforms, transforms), BufferWrite(c, B::DrawRecords, view.DrawRecords),
								BufferWrite(c, B::Vertices, vertexPages[slot]), BufferWrite(c, B::Views, viewsBuffer),
								BufferWrite(c, B::Materials, materials) };
							table->Write(writes);
							auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
							list.BindDescriptorTable(0, retained);
							if (variant == 1)
							{
								list.BindDescriptorTable(ShadowDepthBindings::BindlessSpace, *bindless);
							}
							list.PushConstants(PushStages(*program.Layout), 0, std::as_bytes(std::span(constants)));
							list.BindIndexBuffer(c.Get(indexPages[slot]), 0, Rhi::IndexType::Uint32);
							DrawVisibilityBin(list, commands, counts, bins, bins.GetBin(variant, slot), path);
						}
					}
				}
				list.EndRendering();
			});
		return resources;
	}
} // namespace Swim::Render
