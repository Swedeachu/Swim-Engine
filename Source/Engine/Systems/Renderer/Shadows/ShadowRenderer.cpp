#include "Engine/Systems/Renderer/Shadows/ShadowRenderer.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <stdexcept>
#include <vector>

namespace Swim::Render
{

	namespace
	{

		using S = Rhi::ResourceState;

		// The geometry pages (each once) and the external draws' inputs the shadow depth pass reads.
		void DeclareGeometryReads(RenderGraphBuilder& b, std::span<const GraphBuffer> vertexPages, std::span<const GraphBuffer> indexPages,
			std::span<const ExternalGeometryDraws> external)
		{
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

			for (const auto& draws : external)
			{
				for (const auto buffer : draws.Buffers)
				{
					b.Read(buffer, S::ShaderRead);
				}

				for (const auto buffer : draws.IndirectBuffers)
				{
					b.Read(buffer, S::IndirectArgument);
				}

				for (const auto texture : draws.Textures)
				{
					b.Read(texture, S::ShaderRead);
				}
			}
		}

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

		// One view drawn into its atlas tile: its culled bins and where it lands.
		struct DrawView
		{
			GraphBuffer Commands;
			GraphBuffer Counts;
			GraphBuffer DrawRecords;
			ShadowTile Tile;
			std::uint32_t ViewIndex = 0; // Into the views buffer.
			std::array<float, 16> ViewProjection{};
			bool Whole = true; // Every caster, over a cleared tile (not a dynamic layer over a copied one).
			ShadowRect Dirty;  // Not Whole: the part of the tile redrawn (Width 0: all of it).
		};

		// One depth pass over a set of tiles of one atlas.
		struct DepthPassDesc
		{
			std::string Label;
			std::array<ShadowProgram, 2> Programs; // Opaque, masked.
			ShadowProgram Clear;				   // Null pipeline: the tiles are not cleared first.
			VisibilityBinLayout Bins;
			std::vector<DrawView> Views;
			std::vector<GraphBuffer> VertexPages;
			std::vector<GraphBuffer> IndexPages;
			GraphBuffer Instances;
			GraphBuffer Transforms;
			GraphBuffer ShadowViews;
			GraphBuffer Materials;
			std::uint32_t MaterialCount = 0;
			GraphTexture Atlas;
			std::uint32_t AtlasSize = 0;
			bool LoadAtlas = false; // Otherwise the whole atlas is cleared first.
			VisibilityDrawPath Path = VisibilityDrawPath::IndirectCount;
			Rhi::DescriptorTable* Bindless = nullptr;
			std::vector<ExternalGeometryDraws> External;
			std::vector<ExternalGeometryDraws> StaticExternal; // Only into Whole views.
		};

		void SetTile(Rhi::CommandList& list, const DrawView& view)
		{
			const auto& tile = view.Tile;
			const auto& dirty = view.Dirty;
			list.SetViewport({ float(tile.X), float(tile.Y), float(tile.Size), float(tile.Size), 0.0f, 1.0f });

			if (dirty.Width != 0)
			{
				list.SetScissor({ std::int32_t(dirty.X), std::int32_t(dirty.Y), dirty.Width, dirty.Height });
			}
			else
			{
				list.SetScissor({ std::int32_t(tile.X), std::int32_t(tile.Y), tile.Size, tile.Size });
			}
		}

		std::array<Rhi::DescriptorWrite, ShadowDepthBindings::Count> DepthWrites(
			RenderCommandContext& c, const DepthPassDesc& pass, const DrawView& view, GraphBuffer vertices)
		{
			using B = ShadowDepthBindings;
			return { BufferWrite(c, B::Instances, pass.Instances), BufferWrite(c, B::Transforms, pass.Transforms),
				BufferWrite(c, B::DrawRecords, view.DrawRecords), BufferWrite(c, B::Vertices, vertices),
				BufferWrite(c, B::Views, pass.ShadowViews), BufferWrite(c, B::Materials, pass.Materials) };
		}

		// The view's GPU-culled opaque and masked bins, one draw per page slot.
		void DrawBins(RenderCommandContext& c, const DepthPassDesc& pass, const DrawView& view)
		{
			auto& list = c.Commands();

			for (std::uint32_t variant = 0; variant < 2; ++variant)
			{
				const auto& program = pass.Programs[variant];
				list.BindGraphicsPipeline(*program.Pipeline);
				SetTile(list, view);
				const std::array<std::uint32_t, 4> constants{ view.ViewIndex, pass.MaterialCount, 0, 0 };
				auto& commands = c.Get(view.Commands);
				auto& counts = c.Get(view.Counts);

				for (std::uint32_t slot = 0; slot < pass.VertexPages.size(); ++slot)
				{
					auto table = c.CreateDescriptorTable({ program.Layout, 0, 0, pass.Label });

					if (!table)
					{
						throw std::runtime_error(pass.Label + " descriptor table could not be created");
					}

					table->Write(DepthWrites(c, pass, view, pass.VertexPages[slot]));
					auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
					list.BindDescriptorTable(0, retained);

					if (variant == 1)
					{
						list.BindDescriptorTable(ShadowDepthBindings::BindlessSpace, *pass.Bindless);
					}

					list.PushConstants(PushStages(*program.Layout), 0, std::as_bytes(std::span(constants)));
					list.BindIndexBuffer(c.Get(pass.IndexPages[slot]), 0, Rhi::IndexType::Uint32);
					DrawVisibilityBin(list, commands, counts, pass.Bins, pass.Bins.GetBin(variant, slot), pass.Path);
				}
			}
		}

		void DrawExternal(RenderCommandContext& c, const DepthPassDesc& pass, const DrawView& view)
		{
			ExternalDrawContext context{ c };
			context.Pass = ExternalGeometryPass::Shadow;
			context.Width = pass.AtlasSize;
			context.Height = pass.AtlasSize;
			context.Bindless = pass.Bindless;
			context.ShadowView = view.ViewIndex;
			context.ShadowViewProjection = view.ViewProjection;
			context.FrameTable = [&](Rhi::PipelineLayout& layout) -> Rhi::DescriptorTable&
			{
				auto table = c.CreateDescriptorTable({ &layout, 0, 0, pass.Label + " external" });

				if (!table)
				{
					throw std::runtime_error(pass.Label + " external descriptor table could not be created");
				}

				table->Write(DepthWrites(c, pass, view, pass.VertexPages.front()));
				return static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
			};

			for (const auto* list : { &pass.External, &pass.StaticExternal })
			{
				if (list == &pass.StaticExternal && !view.Whole)
				{
					continue;
				}

				for (const auto& draws : *list)
				{
					SetTile(c.Commands(), view);

					if (draws.Draw)
					{
						draws.Draw(context);
					}
				}
			}
		}

		GraphPass AddDepthPass(RenderGraph& graph, DepthPassDesc pass)
		{
			const std::string passName = pass.Label;
			return graph.AddPass(
				passName, Rhi::QueueType::Graphics,
				[&](RenderGraphBuilder& b)
				{
					for (const auto& view : pass.Views)
					{
						b.Read(view.Commands, S::IndirectArgument);
						b.Read(view.Counts, S::IndirectArgument);
						b.Read(view.DrawRecords, S::ShaderRead);
					}

					for (const auto buffer : { pass.Instances, pass.Transforms, pass.ShadowViews, pass.Materials })
					{
						b.Read(buffer, S::ShaderRead);
					}

					DeclareGeometryReads(b, pass.VertexPages, pass.IndexPages, pass.External);
					DeclareGeometryReads(b, {}, {}, pass.StaticExternal);

					if (pass.LoadAtlas)
					{
						b.ReadWrite(pass.Atlas, S::DepthStencilWrite);
					}
					else
					{
						b.Write(pass.Atlas, S::DepthStencilWrite);
					}
				},
				[pass](RenderCommandContext& c)
				{
					Rhi::TextureViewDesc depthView;
					depthView.PixelFormat = ShadowRenderer::AtlasFormat;
					const Rhi::DepthStencilAttachmentDesc depth{ &c.CreateView(pass.Atlas, depthView),
						pass.LoadAtlas ? Rhi::LoadOp::Load : Rhi::LoadOp::Clear, Rhi::StoreOp::Store,
						DepthClearValue(CanonicalDepthConvention), 0 };
					auto& list = c.Commands();
					list.BeginRendering({ {}, &depth, { pass.AtlasSize, pass.AtlasSize } });

					for (const auto& view : pass.Views)
					{
						if (pass.Clear.Pipeline && view.Whole)
						{
							// The tile's old depth goes: a full-tile triangle at the far depth.
							list.BindGraphicsPipeline(*pass.Clear.Pipeline);
							SetTile(list, view);
							list.Draw(3);
						}

						DrawBins(c, pass, view);

						if (!pass.External.empty() || (view.Whole && !pass.StaticExternal.empty()))
						{
							DrawExternal(c, pass, view);
						}
					}

					list.EndRendering();
				});
		}

		// Each view's static tile, copied into the atlas before its dynamic casters are drawn
		// (loadAtlas: the rest of the atlas keeps its depth; otherwise it holds none yet).
		void AddStaticCopy(RenderGraph& graph, const std::string& name, GraphTexture staticAtlas, GraphTexture atlas,
			std::vector<ShadowRect> regions, bool loadAtlas)
		{
			graph.AddPass(
				name + " static copy", Rhi::QueueType::Graphics,
				[&](RenderGraphBuilder& b)
				{
					b.Read(staticAtlas, S::CopySource);

					if (loadAtlas)
					{
						b.ReadWrite(atlas, S::CopyDestination);
					}
					else
					{
						b.Write(atlas, S::CopyDestination);
					}
				},
				[staticAtlas, atlas, regions](RenderCommandContext& c)
				{
					for (const auto& rect : regions)
					{
						Rhi::TextureCopyRegion region;
						region.SourceOffset = { std::int32_t(rect.X), std::int32_t(rect.Y), 0 };
						region.DestinationOffset = region.SourceOffset;
						region.Extent = { rect.Width, rect.Height, 1 };
						c.Commands().CopyTexture(c.Get(staticAtlas), c.Get(atlas), region);
					}
				});
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

	Rhi::GraphicsPipelineDesc ShadowRenderer::ClearPipelineDesc(Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout)
	{
		Rhi::GraphicsPipelineDesc pipeline = PipelineDesc(program, layout);
		pipeline.DepthStencil.DepthCompare = Rhi::CompareOp::Always;
		pipeline.DebugName = "Shadow tile clear";
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

		const bool persistent = frame.Atlas.has_value();
		const bool loadAtlas = persistent && frame.AtlasHoldsDepth; // Otherwise the whole atlas is cleared first.

		if (persistent && !desc.Clear.Pipeline)
		{
			throw std::invalid_argument(name + " persistent atlas needs the tile clear program");
		}

		if (!frame.Render.empty() && frame.Render.size() != plan.Draws.size())
		{
			throw std::invalid_argument(name + " render flags must match the plan's views");
		}

		const auto renders = [&](std::size_t v)
		{
			return !persistent || frame.Render.empty() || frame.Render[v] != 0;
		};

		ShadowGraphResources resources;
		resources.AtlasSize = plan.AtlasSize;
		resources.ViewCount = static_cast<std::uint32_t>(plan.Views.size());
		resources.RecordCount = static_cast<std::uint32_t>(plan.Records.size());
		resources.Records = graph.CreateUpload(
			std::as_bytes(std::span(plan.Records)), name + " records", Rhi::BufferUsage::Storage, 16, sizeof(GpuShadowRecord));
		const std::vector<GpuShadowView> views = plan.Views.empty() ? std::vector<GpuShadowView>(1) : plan.Views;
		resources.Views =
			graph.CreateUpload(std::as_bytes(std::span(views)), name + " views", Rhi::BufferUsage::Storage, 16, sizeof(GpuShadowView));

		if (persistent)
		{
			const auto& atlasDesc = graph.GetDesc(*frame.Atlas);

			if (atlasDesc.PixelFormat != AtlasFormat || atlasDesc.Extent.Width != plan.AtlasSize || atlasDesc.Extent.Height != plan.AtlasSize)
			{
				throw std::invalid_argument(name + " persistent atlas must be a D32Float texture of the plan's atlas size");
			}

			resources.Atlas = *frame.Atlas;
		}
		else
		{
			Rhi::TextureDesc atlas;
			atlas.Extent = { plan.AtlasSize, plan.AtlasSize, 1 };
			atlas.PixelFormat = AtlasFormat;
			atlas.Usage = Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferSource;
			const std::string atlasName = name + " atlas";
			atlas.DebugName = atlasName;
			resources.Atlas = graph.CreateTexture(atlas);
		}

		// Static layer: the static casters live in their own atlas and are redrawn only when
		// StaticRender says so; every rendered view then copies its static tile and draws the
		// dynamic casters on top.
		const bool layered = persistent && frame.StaticAtlas.has_value();

		if (layered)
		{
			const auto& staticDesc = graph.GetDesc(*frame.StaticAtlas);

			if (frame.StaticRender.size() != plan.Draws.size() || staticDesc.PixelFormat != AtlasFormat ||
				staticDesc.Extent.Width != plan.AtlasSize || staticDesc.Extent.Height != plan.AtlasSize)
			{
				throw std::invalid_argument(name + " static layer needs flags per view and a D32Float atlas of the plan's atlas size");
			}
		}

		// GPU caster culling, one visibility record per rendered view (and layer).
		const auto cull = [&](const ShadowViewPlan& draw, std::size_t v, std::uint32_t layer)
		{
			VisibilityFrameDesc visibilityFrame;
			visibilityFrame.View = BuildGpuViewRecord(draw.Visibility);
			visibilityFrame.View.Flags |= layer;
			visibilityFrame.IndexPages = indexPageIds;
			visibilityFrame.ReadStats = false;
			visibilityFrame.ZeroUnusedCommands = frame.ZeroUnusedCommands;
			resources.Visibility.push_back(frame.Visibility->Record(graph, *frame.Scene, *frame.Geometry, visibilityFrame));
			const auto& visibility = resources.Visibility.back();
			DrawView view{ visibility.Commands, visibility.Counts, visibility.DrawRecords, draw.Tile, static_cast<std::uint32_t>(v) };
			std::copy(std::begin(draw.View.ViewProjection), std::end(draw.View.ViewProjection), view.ViewProjection.begin());
			return view;
		};
		std::vector<DrawView> drawViews;
		std::vector<DrawView> staticViews;

		for (std::size_t v = 0; v < plan.Draws.size(); ++v)
		{
			if (!renders(v))
			{
				continue;
			}

			const auto& draw = plan.Draws[v];

			// A static atlas without depth yet redraws every layered tile it is copied from.
			std::uint8_t layer = layered ? frame.StaticRender[v] : 2;
			layer = layer == 0 && !frame.StaticAtlasHoldsDepth ? 1 : layer;

			if (layer == 1)
			{
				staticViews.push_back(cull(draw, v, std::uint32_t(GpuViewFlags::StaticCasters)));
			}

			drawViews.push_back(cull(draw, v, layer < 2 ? std::uint32_t(GpuViewFlags::DynamicCasters) : 0u));
			drawViews.back().Whole = layer == 2;

			if (layer == 0 && v < frame.StaticRects.size())
			{
				drawViews.back().Dirty = frame.StaticRects[v];
			}
		}

		resources.RenderedViews = static_cast<std::uint32_t>(drawViews.size());

		if (persistent && drawViews.empty())
		{
			return resources; // Every tile is cached: nothing to draw.
		}

		DepthPassDesc pass;
		pass.Programs = { desc.Opaque, desc.Masked };
		pass.Bins = bins;
		pass.VertexPages = vertexPages;
		pass.IndexPages = indexPages;
		pass.Instances = frame.Scene->Instances;
		pass.Transforms = frame.Scene->Transforms;
		pass.ShadowViews = resources.Views;
		pass.Materials = frame.Materials->Materials;
		pass.MaterialCount = frame.Materials->MaterialCount;
		pass.AtlasSize = plan.AtlasSize;
		pass.Path = desc.DrawPath;
		pass.Bindless = frame.Bindless;

		for (const auto& draws : frame.External)
		{
			if ((draws.Passes & ExternalGeometryPassBit(ExternalGeometryPass::Shadow)) != 0)
			{
				(layered && draws.StaticShadow ? pass.StaticExternal : pass.External).push_back(draws);
			}
		}

		bool copied = false;

		if (layered)
		{
			if (!staticViews.empty())
			{
				DepthPassDesc staticPass = pass;
				staticPass.Label = name + " static depth";
				staticPass.Clear = desc.Clear;
				staticPass.Views = std::move(staticViews);
				staticPass.Atlas = *frame.StaticAtlas;
				staticPass.LoadAtlas = frame.StaticAtlasHoldsDepth;
				staticPass.External = std::move(staticPass.StaticExternal);
				staticPass.StaticExternal.clear();
				AddDepthPass(graph, std::move(staticPass));
			}

			std::vector<ShadowRect> regions;

			for (const auto& view : drawViews)
			{
				if (!view.Whole)
				{
					const auto& tile = view.Tile;
					regions.push_back(view.Dirty.Width != 0 ? view.Dirty : ShadowRect{ tile.X, tile.Y, tile.Size, tile.Size });
				}
			}

			copied = !regions.empty();

			if (copied)
			{
				AddStaticCopy(graph, name, *frame.StaticAtlas, resources.Atlas, std::move(regions), loadAtlas);
			}
		}

		pass.Label = name + " depth";
		pass.Clear = persistent ? desc.Clear : ShadowProgram{};
		pass.Views = std::move(drawViews);
		pass.Atlas = resources.Atlas;
		pass.LoadAtlas = copied || loadAtlas; // Otherwise every view is drawn whole over a cleared atlas.
		resources.DepthPass = AddDepthPass(graph, std::move(pass));
		return resources;
	}

} // namespace Swim::Render
