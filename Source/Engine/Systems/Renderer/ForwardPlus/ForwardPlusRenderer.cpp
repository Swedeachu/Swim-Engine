#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRenderer.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentBindings.h"
#include "Engine/Systems/Renderer/Shadows/ShadowRecords.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;

		constexpr std::array<Rhi::Format, 2> OpaqueFormats{ ForwardPlusRenderer::ColorFormat, ForwardPlusRenderer::ObjectIdFormat };
		constexpr std::array<Rhi::Format, 1> TransparentFormats{ ForwardPlusRenderer::ColorFormat };
		constexpr std::array<Rhi::BlendAttachmentState, 2> OpaqueBlends{};
		constexpr std::array<Rhi::BlendAttachmentState, 1> TransparentBlends{ { { true, Rhi::BlendFactor::One,
			Rhi::BlendFactor::OneMinusSourceAlpha, Rhi::BlendOp::Add, Rhi::BlendFactor::One, Rhi::BlendFactor::OneMinusSourceAlpha,
			Rhi::BlendOp::Add, Rhi::ColorWriteMask::All } } };

		constexpr std::uint64_t CommandBytes = sizeof(Rhi::DrawIndexedIndirectCommand);

		bool HasUsage(Rhi::TextureUsage usage, Rhi::TextureUsage required)
		{
			return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(required)) != 0;
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

		Rhi::BufferDesc StorageBuffer(std::uint64_t size, Rhi::BufferUsage extra, const std::string& name)
		{
			Rhi::BufferDesc buffer;
			buffer.Size = size;
			buffer.Usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | extra;
			buffer.DebugName = name;
			return buffer;
		}

		std::uint32_t NextPowerOfTwo(std::uint32_t value)
		{
			std::uint32_t result = 1;
			while (result < value)
			{
				result <<= 1;
			}
			return result;
		}

		// The graph handles every draw pass binds in space 0 (the vertex page varies per slot).
		struct DrawInputs
		{
			GraphBuffer Instances;
			GraphBuffer Transforms;
			GraphBuffer DrawRecords;
			GraphBuffer View;
			GraphBuffer Materials;
			GraphBuffer Lights;
			GraphBuffer LightHeader;
			GraphBuffer Grid;
			GraphBuffer Records;
			GraphBuffer Indices;
			GraphBuffer Irradiance;
			GraphTexture Prefiltered;
			GraphTexture BrdfLut;
			std::uint32_t PrefilteredMipCount = 1;
			GraphTexture ShadowAtlas;
			bool ShadowAtlasIsDepth = false; // D32 (depth aspect) or the R32Float stand-in.
			GraphBuffer ShadowRecords;
			GraphBuffer ShadowViews;
			std::vector<GraphBuffer> VertexPages; // Per slot.
			std::vector<GraphBuffer> IndexPages;  // Per slot.
		};

		void DeclareDrawReads(RenderGraphBuilder& b, const DrawInputs& inputs)
		{
			for (const auto buffer :
				{ inputs.Instances, inputs.Transforms, inputs.DrawRecords, inputs.View, inputs.Materials, inputs.Lights, inputs.LightHeader,
					inputs.Grid, inputs.Records, inputs.Indices, inputs.Irradiance, inputs.ShadowRecords, inputs.ShadowViews })
			{
				b.Read(buffer, S::ShaderRead);
			}
			b.Read(inputs.Prefiltered, S::ShaderRead);
			b.Read(inputs.BrdfLut, S::ShaderRead);
			b.Read(inputs.ShadowAtlas, S::ShaderRead);
			std::vector<GraphBuffer> declared;
			const auto once = [&](GraphBuffer page, S state)
			{
				if (std::find(declared.begin(), declared.end(), page) == declared.end())
				{
					declared.push_back(page);
					b.Read(page, state);
				}
			};
			for (const auto page : inputs.VertexPages)
			{
				once(page, S::ShaderRead);
			}
			for (const auto page : inputs.IndexPages)
			{
				once(page, S::IndexBuffer);
			}
		}

		// Space-0 tables of one program, one per page slot.
		std::vector<Rhi::DescriptorTable*> CreateDrawTables(
			RenderCommandContext& c, const DrawInputs& inputs, Rhi::PipelineLayout& layout, Rhi::Sampler& sampler, const std::string& label)
		{
			using B = ForwardPlusDrawBindings;
			Rhi::TextureViewDesc cubeView;
			cubeView.Dimension = Rhi::TextureViewDimension::TextureCube;
			cubeView.PixelFormat = Rhi::Format::RGBA16Float;
			cubeView.MipLevelCount = inputs.PrefilteredMipCount;
			cubeView.ArrayLayerCount = 6;
			Rhi::TextureViewDesc lutView;
			lutView.PixelFormat = Rhi::Format::RGBA16Float;
			auto& prefiltered = c.CreateView(inputs.Prefiltered, cubeView);
			auto& lut = c.CreateView(inputs.BrdfLut, lutView);
			Rhi::TextureViewDesc shadowView;
			shadowView.PixelFormat = inputs.ShadowAtlasIsDepth ? Rhi::Format::D32Float : Rhi::Format::R32Float;
			shadowView.Aspect = inputs.ShadowAtlasIsDepth ? Rhi::TextureAspect::Depth : Rhi::TextureAspect::Automatic;
			auto& shadowAtlas = c.CreateView(inputs.ShadowAtlas, shadowView);
			std::vector<Rhi::DescriptorTable*> tables;
			for (std::size_t slot = 0; slot < inputs.VertexPages.size(); ++slot)
			{
				auto table = c.Device().CreateDescriptorTable({ &layout, 0, 0, label });
				if (!table)
				{
					throw std::runtime_error(label + " descriptor table could not be created");
				}
				std::array<Rhi::DescriptorWrite, B::Count> writes{ BufferWrite(c, B::Instances, inputs.Instances),
					BufferWrite(c, B::Transforms, inputs.Transforms), BufferWrite(c, B::DrawRecords, inputs.DrawRecords),
					BufferWrite(c, B::Vertices, inputs.VertexPages[slot]), BufferWrite(c, B::View, inputs.View),
					BufferWrite(c, B::Materials, inputs.Materials), BufferWrite(c, B::Lights, inputs.Lights),
					BufferWrite(c, B::LightHeader, inputs.LightHeader), BufferWrite(c, B::ClusterGrid, inputs.Grid),
					BufferWrite(c, B::ClusterRecords, inputs.Records), BufferWrite(c, B::ClusterIndices, inputs.Indices),
					BufferWrite(c, B::EnvironmentIrradiance, inputs.Irradiance) };
				writes[B::ShadowRecords] = BufferWrite(c, B::ShadowRecords, inputs.ShadowRecords);
				writes[B::ShadowViews] = BufferWrite(c, B::ShadowViews, inputs.ShadowViews);
				writes[B::ShadowAtlas].Binding = B::ShadowAtlas;
				writes[B::ShadowAtlas].TextureResource = &shadowAtlas;
				writes[B::EnvironmentPrefiltered].Binding = B::EnvironmentPrefiltered;
				writes[B::EnvironmentPrefiltered].TextureResource = &prefiltered;
				writes[B::EnvironmentBrdfLut].Binding = B::EnvironmentBrdfLut;
				writes[B::EnvironmentBrdfLut].TextureResource = &lut;
				writes[B::EnvironmentSampler].Binding = B::EnvironmentSampler;
				writes[B::EnvironmentSampler].SamplerResource = &sampler;
				table->Write(writes);
				tables.push_back(&static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table))));
			}
			return tables;
		}

		void ValidateTarget(const RenderGraph& graph, GraphTexture target, Rhi::Format format, Rhi::TextureUsage usage, std::uint32_t width,
			std::uint32_t height, const std::string& what)
		{
			const auto& desc = graph.GetDesc(target);
			if (desc.Dimension != Rhi::TextureDimension::Texture2D || desc.PixelFormat != format || desc.Samples != Rhi::SampleCount::X1 ||
				desc.Extent.Width != width || desc.Extent.Height != height || desc.ArrayLayers != 1 || !HasUsage(desc.Usage, usage))
			{
				throw std::invalid_argument(what + " must be a single-sample viewport-sized 2D attachment of the documented format");
			}
		}
	} // namespace

	Rhi::GraphicsPipelineDesc ForwardPlusRenderer::PipelineDesc(
		ForwardPlusBin bin, Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout)
	{
		Rhi::GraphicsPipelineDesc pipeline{};
		pipeline.Program = &program;
		pipeline.Layout = &layout;
		pipeline.DepthStencilFormat = CanonicalDepthFormat;
		pipeline.DepthStencil.DepthTest = true;
		pipeline.DepthStencil.DepthCompare = DepthCompareOp(CanonicalDepthConvention);
		pipeline.Raster.Cull = Rhi::CullMode::None;
		pipeline.Raster.Winding = Rhi::FrontFace::CounterClockwise;
		if (bin == ForwardPlusBin::Opaque)
		{
			pipeline.ColorFormats = OpaqueFormats;
			pipeline.BlendAttachments = OpaqueBlends;
			pipeline.DepthStencil.DepthWrite = true;
			pipeline.DebugName = "Forward+ opaque";
		}
		else if (bin == ForwardPlusBin::Transparent)
		{
			pipeline.ColorFormats = TransparentFormats;
			pipeline.BlendAttachments = TransparentBlends;
			pipeline.DepthStencil.DepthWrite = false;
			pipeline.DebugName = "Forward+ transparent";
		}
		else
		{
			throw std::invalid_argument("Unknown Forward+ bin");
		}
		return pipeline;
	}

	std::vector<std::uint32_t> ForwardPlusRenderer::VisibilityBinCapacities(std::uint32_t opaque, std::uint32_t transparent)
	{
		if (!opaque || !transparent || transparent > ForwardTransparentSortBindings::MaxDraws)
		{
			throw std::invalid_argument("Forward+ bins need nonzero capacities and at most MaxDraws transparent draws per page slot");
		}
		return { opaque, transparent };
	}

	void ForwardPlusRenderer::RouteMaterial(GpuVisibility& visibility, std::uint32_t materialSet, const StandardPbr::Parameters& parameters)
	{
		visibility.SetMaterialBin(materialSet, static_cast<std::uint32_t>(ForwardPlus::MaterialBin(parameters)));
	}

	ForwardPlusRenderer::ForwardPlusRenderer(Rhi::Device& device, ForwardPlusRendererDesc descInput) : desc(std::move(descInput))
	{
		if (!desc.Opaque.Pipeline || !desc.Opaque.Layout || !desc.Transparent.Pipeline || !desc.Transparent.Layout || !desc.SortPipeline ||
			!desc.SortLayout)
		{
			throw std::invalid_argument(desc.DebugName + " needs the opaque, transparent and sort programs");
		}
		Rhi::SamplerDesc sampler{};
		sampler.AddressU = sampler.AddressV = sampler.AddressW = Rhi::SamplerAddressMode::ClampToEdge;
		environmentSampler = device.CreateSampler(sampler);
		if (!environmentSampler)
		{
			throw std::runtime_error(desc.DebugName + " environment sampler could not be created");
		}
	}

	ForwardPlusRenderer::~ForwardPlusRenderer() = default;

	ForwardPlusGraphResources ForwardPlusRenderer::Record(
		RenderGraph& graph, const ForwardPlusFrame& frame, const ForwardPlusTargets& targets) const
	{
		const auto& name = desc.DebugName;
		if (!frame.Scene || !frame.Geometry || !frame.Visibility || !frame.Visibility->Bins || !frame.Materials || !frame.Bindless ||
			!frame.Lights || !frame.Clusters || frame.PageSlots.empty() || (frame.Environment && !frame.BrdfLut))
		{
			throw std::invalid_argument(name +
				" frame needs the scene, geometry, visibility, materials, bindless table, lights, clusters, "
				"page slots and (with an environment) a BRDF LUT");
		}
		const auto slots = static_cast<std::uint32_t>(frame.PageSlots.size());
		const auto& bins = *frame.Visibility->Bins;
		if (bins.GetMaterialBins() != ForwardPlusBinCount || bins.GetPageSlots() != slots)
		{
			throw std::invalid_argument(name + " needs ForwardPlusBinCount material bins over exactly the frame's page slots");
		}
		DrawInputs inputs;
		for (const auto& slot : frame.PageSlots)
		{
			if (slot.IndexPage >= frame.Geometry->Pages.size() || slot.VertexPage >= frame.Geometry->Pages.size() ||
				slot.IndexPage == slot.VertexPage)
			{
				throw std::invalid_argument(name + " page slot names a missing GeometryHeap page");
			}
			inputs.IndexPages.push_back(frame.Geometry->Pages[slot.IndexPage]);
			inputs.VertexPages.push_back(frame.Geometry->Pages[slot.VertexPage]);
		}

		// Targets are the cluster grid's viewport.
		const std::uint32_t width = frame.Clusters->GridRecord.Limits[0];
		const std::uint32_t height = frame.Clusters->GridRecord.Limits[1];
		ValidateTarget(graph, targets.Color, ColorFormat, Rhi::TextureUsage::ColorAttachment, width, height, name + " color target");
		ValidateTarget(
			graph, targets.ObjectId, ObjectIdFormat, Rhi::TextureUsage::ColorAttachment, width, height, name + " object-id target");
		ValidateTarget(
			graph, targets.Depth, CanonicalDepthFormat, Rhi::TextureUsage::DepthStencilAttachment, width, height, name + " depth target");

		// Transparent bins: equal capacities, consecutive per slot (VisibilityBinLayout).
		const auto transparentBin = static_cast<std::uint32_t>(ForwardPlusBin::Transparent) * slots;
		const auto& firstRange = bins.GetRange(transparentBin);
		const std::uint32_t capacity = firstRange.Capacity;
		for (std::uint32_t slot = 0; slot < slots; ++slot)
		{
			const auto& range = bins.GetRange(transparentBin + slot);
			if (range.Capacity != capacity || range.First != firstRange.First + slot * capacity)
			{
				throw std::invalid_argument(name + " transparent bins must be consecutive with equal capacities");
			}
		}
		if (!capacity || capacity > ForwardTransparentSortBindings::MaxDraws)
		{
			throw std::invalid_argument(name + " transparent capacity must be 1 .. ForwardTransparentSortBindings::MaxDraws per page slot");
		}

		ForwardPlusGraphResources resources;
		resources.TransparentCapacity = capacity;
		resources.SortSize = NextPowerOfTwo(capacity);
		resources.ViewRecord = BuildForwardViewRecord(frame.View, frame.Materials->MaterialCount,
			frame.Environment ? frame.Environment->PrefilteredMipCount : 1u, frame.Environment != nullptr, frame.Shadows != nullptr);
		resources.View =
			graph.CreateUpload(std::as_bytes(std::span(&resources.ViewRecord, 1)), name + " view", Rhi::BufferUsage::Storage, 16);

		// Environment or 1x1 zero stand-ins (never sampled: the view flag is clear).
		if (frame.Environment)
		{
			inputs.Irradiance = frame.Environment->Irradiance;
			inputs.Prefiltered = frame.Environment->Prefiltered;
			inputs.BrdfLut = *frame.BrdfLut;
			inputs.PrefilteredMipCount = frame.Environment->PrefilteredMipCount;
		}
		else
		{
			resources.EnvironmentFallback = true;
			const std::array<std::byte, EnvironmentIrradianceBindings::OutputBytes> zeros{};
			inputs.Irradiance = graph.CreateUpload(zeros, name + " null irradiance", Rhi::BufferUsage::Storage, 16);
			Rhi::TextureDesc cube;
			cube.Dimension = Rhi::TextureDimension::TextureCube;
			cube.Extent = { 1, 1, 1 };
			cube.PixelFormat = Rhi::Format::RGBA16Float;
			cube.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			cube.ArrayLayers = 6;
			const std::string cubeName = name + " null environment";
			cube.DebugName = cubeName;
			inputs.Prefiltered = graph.CreateTexture(cube);
			Rhi::TextureDesc lut;
			lut.Extent = { 1, 1, 1 };
			lut.PixelFormat = Rhi::Format::RGBA16Float;
			lut.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string lutName = name + " null BRDF LUT";
			lut.DebugName = lutName;
			inputs.BrdfLut = graph.CreateTexture(lut);
			const std::array<std::byte, 8> texel{};
			for (std::uint32_t face = 0; face < 6; ++face)
			{
				AddTextureUpload(graph, cubeName + " upload", texel, inputs.Prefiltered, { 0, { 0, face }, {}, { 1, 1, 1 } });
			}
			AddTextureUpload(graph, lutName + " upload", texel, inputs.BrdfLut, { 0, {}, {}, { 1, 1, 1 } });
		}
		// Shadows or stand-ins (never sampled: the view flag is clear).
		if (frame.Shadows)
		{
			inputs.ShadowAtlas = frame.Shadows->Atlas;
			inputs.ShadowAtlasIsDepth = graph.GetDesc(frame.Shadows->Atlas).PixelFormat == Rhi::Format::D32Float;
			inputs.ShadowRecords = frame.Shadows->Records;
			inputs.ShadowViews = frame.Shadows->Views;
		}
		else
		{
			resources.ShadowFallback = true;
			const std::array<std::byte, sizeof(GpuShadowRecord)> record{};
			inputs.ShadowRecords = graph.CreateUpload(record, name + " null shadow records", Rhi::BufferUsage::Storage, 16);
			const std::array<std::byte, sizeof(GpuShadowView)> view{};
			inputs.ShadowViews = graph.CreateUpload(view, name + " null shadow views", Rhi::BufferUsage::Storage, 16);
			Rhi::TextureDesc atlas;
			atlas.Extent = { 1, 1, 1 };
			atlas.PixelFormat = Rhi::Format::R32Float;
			atlas.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			const std::string atlasName = name + " null shadow atlas";
			atlas.DebugName = atlasName;
			inputs.ShadowAtlas = graph.CreateTexture(atlas);
			const std::array<std::byte, 4> texel{};
			AddTextureUpload(graph, atlasName + " upload", texel, inputs.ShadowAtlas, { 0, {}, {}, { 1, 1, 1 } });
		}
		inputs.Instances = frame.Scene->Instances;
		inputs.Transforms = frame.Scene->Transforms;
		inputs.DrawRecords = frame.Visibility->DrawRecords;
		inputs.View = resources.View;
		inputs.Materials = frame.Materials->Materials;
		inputs.Lights = frame.Lights->Lights;
		inputs.LightHeader = frame.Lights->Header;
		inputs.Grid = frame.Clusters->Grid;
		inputs.Records = frame.Clusters->Records;
		inputs.Indices = frame.Clusters->Indices;

		const auto commands = frame.Visibility->Commands;
		const auto counts = frame.Visibility->Counts;
		const auto path = desc.DrawPath;
		auto* bindless = frame.Bindless;
		auto* sampler = environmentSampler.get();

		// 1. Opaque.
		resources.OpaquePass = graph.AddPass(
			name + " opaque", Rhi::QueueType::Graphics,
			[&](RenderGraphBuilder& b)
			{
				DeclareDrawReads(b, inputs);
				b.Read(commands, S::IndirectArgument);
				b.Read(counts, S::IndirectArgument);
				if (targets.Clear)
				{
					b.Write(targets.Color, S::ColorAttachment);
					b.Write(targets.ObjectId, S::ColorAttachment);
					b.Write(targets.Depth, S::DepthStencilWrite);
				}
				else
				{
					b.ReadWrite(targets.Color, S::ColorAttachment);
					b.ReadWrite(targets.ObjectId, S::ColorAttachment);
					b.ReadWrite(targets.Depth, S::DepthStencilWrite);
				}
			},
			[program = desc.Opaque, label = name + " opaque", inputs, targets, commands, counts, bins, path, bindless, sampler, slots,
				width, height](RenderCommandContext& c)
			{
				const auto tables = CreateDrawTables(c, inputs, *program.Layout, *sampler, label);
				const auto load = targets.Clear ? Rhi::LoadOp::Clear : Rhi::LoadOp::Load;
				std::array<Rhi::RenderingAttachmentDesc, 2> colors{};
				colors[0].View = &c.CreateView(targets.Color);
				colors[0].Load = load;
				colors[0].Clear.Value = targets.ClearColor;
				colors[1].View = &c.CreateView(targets.ObjectId);
				colors[1].Load = load;
				Rhi::TextureViewDesc depthView;
				depthView.PixelFormat = CanonicalDepthFormat;
				const Rhi::DepthStencilAttachmentDesc depth{ &c.CreateView(targets.Depth, depthView), load, Rhi::StoreOp::Store,
					DepthClearValue(CanonicalDepthConvention), 0 };
				auto& list = c.Commands();
				list.BeginRendering({ colors, &depth, { width, height } });
				list.BindGraphicsPipeline(*program.Pipeline);
				list.SetViewport({ 0, 0, float(width), float(height) });
				list.SetScissor({ 0, 0, width, height });
				auto& commandBuffer = c.Get(commands);
				auto& countBuffer = c.Get(counts);
				for (std::uint32_t slot = 0; slot < slots; ++slot)
				{
					list.BindDescriptorTable(0, *tables[slot]);
					list.BindDescriptorTable(ForwardPlusDrawBindings::BindlessSpace, *bindless);
					list.BindIndexBuffer(c.Get(inputs.IndexPages[slot]), 0, Rhi::IndexType::Uint32);
					DrawVisibilityBin(list, commandBuffer, countBuffer, bins,
						bins.GetBin(static_cast<std::uint32_t>(ForwardPlusBin::Opaque), slot), path);
				}
				list.EndRendering();
			});

		// 2. Sort the transparent bins.
		resources.SortScratch = graph.CreateBuffer(StorageBuffer(
			std::uint64_t(slots) * resources.SortSize * sizeof(ForwardSortEntry), Rhi::BufferUsage::None, name + " sort scratch"));
		resources.SortedCommands = graph.CreateBuffer(
			StorageBuffer(std::uint64_t(slots) * capacity * CommandBytes, Rhi::BufferUsage::Indirect, name + " sorted commands"));
		resources.SortedCounts = graph.CreateBuffer(
			StorageBuffer(std::uint64_t(slots) * sizeof(std::uint32_t), Rhi::BufferUsage::Indirect, name + " sorted counts"));
		const auto r = resources;
		resources.SortPass = graph.AddPass(
			name + " transparent sort", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(commands, S::ShaderRead);
				b.Read(inputs.DrawRecords, S::ShaderRead);
				b.Read(counts, S::ShaderRead);
				b.Read(inputs.Instances, S::ShaderRead);
				b.Read(inputs.Transforms, S::ShaderRead);
				b.Read(r.View, S::ShaderRead);
				b.Write(r.SortScratch, S::ShaderWrite);
				b.Write(r.SortedCommands, S::ShaderWrite);
				b.Write(r.SortedCounts, S::ShaderWrite);
			},
			[pipeline = desc.SortPipeline, layout = desc.SortLayout, label = name + " transparent sort", r, commands, counts, inputs,
				transparentBin, first = firstRange.First, capacity, slots](RenderCommandContext& c)
			{
				using B = ForwardTransparentSortBindings;
				auto table = c.Device().CreateDescriptorTable({ layout, 0, 0, label });
				if (!table)
				{
					throw std::runtime_error(label + " descriptor table could not be created");
				}
				const std::array<Rhi::DescriptorWrite, B::Count> writes{ BufferWrite(c, B::Commands, commands),
					BufferWrite(c, B::DrawRecords, inputs.DrawRecords), BufferWrite(c, B::Counts, counts),
					BufferWrite(c, B::Instances, inputs.Instances), BufferWrite(c, B::Transforms, inputs.Transforms),
					BufferWrite(c, B::View, r.View), BufferWrite(c, B::Scratch, r.SortScratch),
					BufferWrite(c, B::SortedCommands, r.SortedCommands), BufferWrite(c, B::SortedCounts, r.SortedCounts) };
				table->Write(writes);
				auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
				auto& list = c.Commands();
				list.BindComputePipeline(*pipeline);
				list.BindDescriptorTable(0, retained);
				const std::array<std::uint32_t, 4> constants{ transparentBin, first, capacity, r.SortSize };
				list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
				list.Dispatch(slots, 1, 1);
			});

		// 3. Transparent, blended over the opaque result.
		resources.TransparentPass = graph.AddPass(
			name + " transparent", Rhi::QueueType::Graphics,
			[&](RenderGraphBuilder& b)
			{
				DeclareDrawReads(b, inputs);
				b.Read(r.SortedCommands, S::IndirectArgument);
				b.Read(r.SortedCounts, S::IndirectArgument);
				b.ReadWrite(targets.Color, S::ColorAttachment);
				// The attachment layout is the writable one; the pipeline never writes depth.
				b.ReadWrite(targets.Depth, S::DepthStencilWrite);
			},
			[program = desc.Transparent, label = name + " transparent", inputs, targets, r, path, bindless, sampler, capacity, slots, width,
				height](RenderCommandContext& c)
			{
				const auto tables = CreateDrawTables(c, inputs, *program.Layout, *sampler, label);
				Rhi::RenderingAttachmentDesc color{};
				color.View = &c.CreateView(targets.Color);
				color.Load = Rhi::LoadOp::Load;
				Rhi::TextureViewDesc depthView;
				depthView.PixelFormat = CanonicalDepthFormat;
				const Rhi::DepthStencilAttachmentDesc depth{ &c.CreateView(targets.Depth, depthView), Rhi::LoadOp::Load,
					Rhi::StoreOp::Store, DepthClearValue(CanonicalDepthConvention), 0 };
				auto& list = c.Commands();
				list.BeginRendering({ { &color, 1 }, &depth, { width, height } });
				list.BindGraphicsPipeline(*program.Pipeline);
				list.SetViewport({ 0, 0, float(width), float(height) });
				list.SetScissor({ 0, 0, width, height });
				auto& sorted = c.Get(r.SortedCommands);
				auto& sortedCounts = c.Get(r.SortedCounts);
				for (std::uint32_t slot = 0; slot < slots; ++slot)
				{
					list.BindDescriptorTable(0, *tables[slot]);
					list.BindDescriptorTable(ForwardPlusDrawBindings::BindlessSpace, *bindless);
					list.BindIndexBuffer(c.Get(inputs.IndexPages[slot]), 0, Rhi::IndexType::Uint32);
					const std::uint64_t offset = std::uint64_t(slot) * capacity * CommandBytes;
					if (path == VisibilityDrawPath::IndirectCount)
					{
						list.DrawIndexedIndirectCount(sorted, offset, sortedCounts, std::uint64_t(slot) * sizeof(std::uint32_t), capacity);
					}
					else
					{
						list.DrawIndexedIndirect(sorted, offset, capacity); // The sort zero-fills unused slots.
					}
				}
				list.EndRendering();
			});
		return resources;
	}
} // namespace Swim::Render
