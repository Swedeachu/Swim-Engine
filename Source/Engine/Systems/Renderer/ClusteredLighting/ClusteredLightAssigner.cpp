#include "Engine/Systems/Renderer/ClusteredLighting/ClusteredLightAssigner.h"
#include "Engine/Systems/Renderer/Lights/GpuLightRecord.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;

		std::uint32_t Groups(std::uint32_t count, std::uint32_t group)
		{
			return (count + group - 1) / group;
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

		void Bind(
			RenderCommandContext& c, const ClusterProgram& program, const std::string& label, std::span<const Rhi::DescriptorWrite> writes)
		{
			auto table = c.Device().CreateDescriptorTable({ program.Layout, program.Space, 0, label });
			if (!table)
			{
				throw std::runtime_error(label + " descriptor table could not be created");
			}
			table->Write(writes);
			auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
			auto& list = c.Commands();
			list.BindComputePipeline(*program.Pipeline);
			list.BindDescriptorTable(program.Space, retained);
		}

		Rhi::BufferDesc StorageBuffer(std::uint64_t size, const std::string& name)
		{
			Rhi::BufferDesc buffer;
			buffer.Size = size;
			buffer.Usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource;
			buffer.DebugName = name;
			return buffer;
		}
	} // namespace

	ClusteredLightAssigner::ClusteredLightAssigner(ClusteredLightAssignerDesc descInput) : desc(std::move(descInput))
	{
		for (const auto* program : { &desc.Cull, &desc.Bounds, &desc.Assign, &desc.Scan })
		{
			if (!program->Pipeline || !program->Layout)
			{
				throw std::invalid_argument(desc.DebugName + " needs the cull, bounds, assign and scan programs");
			}
		}
		if ((desc.Heatmap.Pipeline == nullptr) != (desc.Heatmap.Layout == nullptr))
		{
			throw std::invalid_argument(desc.DebugName + " heatmap needs both a pipeline and a layout");
		}
	}

	ClusterGraphResources ClusteredLightAssigner::Record(
		RenderGraph& graph, const GpuLightGraphResources& lights, const ClusterGridDesc& gridDesc, const ClusterView& view) const
	{
		ClusterGraphResources resources;
		resources.GridRecord = MakeClusterGridRecord(gridDesc, view);
		resources.Layout = ComputeClusterGridLayout(gridDesc);
		if (lights.RowCount <= lights.FirstLocalRow)
		{
			throw std::invalid_argument(desc.DebugName + " needs a light buffer with local rows");
		}
		resources.LocalLightCapacity = lights.RowCount - lights.FirstLocalRow;
		const std::uint32_t clusters = resources.Layout.ClusterCount;
		const std::uint32_t localCount = lights.LocalCount;

		resources.Grid =
			graph.CreateUpload(std::as_bytes(std::span(&resources.GridRecord, 1)), desc.DebugName + " grid", Rhi::BufferUsage::Storage, 16);
		resources.ViewLights =
			graph.CreateBuffer(StorageBuffer(std::uint64_t(resources.LocalLightCapacity) * 16, desc.DebugName + " view lights"));
		resources.Bounds = graph.CreateBuffer(StorageBuffer(std::uint64_t(clusters) * 32, desc.DebugName + " bounds"));
		resources.Records = graph.CreateBuffer(StorageBuffer(std::uint64_t(clusters) * sizeof(ClusterRecord), desc.DebugName + " records"));
		if (localCount > std::uint64_t(ClusterMaskWords(resources.GridRecord)) * 32u)
		{
			throw std::invalid_argument(desc.DebugName + ": more local lights than the grid's LightCapacity");
		}
		const std::uint64_t blockWords = ClusterBlockWords(resources.GridRecord);
		resources.Indices = graph.CreateBuffer(StorageBuffer(std::uint64_t(clusters) * blockWords * 4, desc.DebugName + " light masks"));
		resources.Stats = graph.CreateBuffer(StorageBuffer(sizeof(ClusterStats), desc.DebugName + " stats"));
		const auto r = resources; // Handles captured by the pass callbacks.

		// 1. Cull.
		resources.Passes.push_back(graph.AddPass(
			desc.DebugName + " cull", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(r.Grid, S::ShaderRead);
				b.Read(lights.Lights, S::ShaderRead);
				b.Read(lights.Header, S::ShaderRead);
				b.Write(r.ViewLights, S::ShaderWrite);
			},
			[program = desc.Cull, label = desc.DebugName + " cull", r, lightBuffer = lights.Lights, header = lights.Header, localCount](
				RenderCommandContext& c)
			{
				const std::array<Rhi::DescriptorWrite, 4> writes{ BufferWrite(c, ClusterLightCullBindings::Grid, r.Grid),
					BufferWrite(c, ClusterLightCullBindings::Lights, lightBuffer),
					BufferWrite(c, ClusterLightCullBindings::LightHeader, header),
					BufferWrite(c, ClusterLightCullBindings::ViewLights, r.ViewLights) };
				Bind(c, program, label, writes);
				c.Commands().Dispatch(Groups(localCount, ClusterLightCullBindings::ThreadGroupSize), 1, 1);
			}));

		// 2. Bounds.
		resources.Passes.push_back(graph.AddPass(
			desc.DebugName + " bounds", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(r.Grid, S::ShaderRead);
				b.Write(r.Bounds, S::ShaderWrite);
			},
			[program = desc.Bounds, label = desc.DebugName + " bounds", r, clusters](RenderCommandContext& c)
			{
				const std::array<Rhi::DescriptorWrite, 2> writes{ BufferWrite(c, ClusterBoundsBindings::Grid, r.Grid),
					BufferWrite(c, ClusterBoundsBindings::Bounds, r.Bounds) };
				Bind(c, program, label, writes);
				c.Commands().Dispatch(Groups(clusters, ClusterBoundsBindings::ThreadGroupSize), 1, 1);
			}));

		// 3. Masks: one thread per (cluster, mask word), 2D past 65535 groups.
		const std::uint64_t maskThreads = std::uint64_t(clusters) * ClusterMaskWords(resources.GridRecord);
		const std::uint64_t maskGroups =
			(maskThreads + ClusterAssignBindings::ThreadGroupSize - 1) / ClusterAssignBindings::ThreadGroupSize;
		const auto groupsX = static_cast<std::uint32_t>(std::min<std::uint64_t>(maskGroups, ClusterAssignBindings::MaxGroupsPerRow));
		const auto groupsY = static_cast<std::uint32_t>((maskGroups + groupsX - 1) / groupsX);
		resources.Passes.push_back(graph.AddPass(
			desc.DebugName + " masks", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(r.Grid, S::ShaderRead);
				b.Read(lights.Header, S::ShaderRead);
				b.Read(r.ViewLights, S::ShaderRead);
				b.Read(r.Bounds, S::ShaderRead);
				b.Write(r.Indices, S::ShaderWrite);
				b.Write(r.Stats, S::ShaderWrite);
			},
			[program = desc.Assign, label = desc.DebugName + " masks", r, header = lights.Header, groupsX, groupsY](RenderCommandContext& c)
			{
				const std::array<Rhi::DescriptorWrite, 6> writes{ BufferWrite(c, ClusterAssignBindings::Grid, r.Grid),
					BufferWrite(c, ClusterAssignBindings::LightHeader, header),
					BufferWrite(c, ClusterAssignBindings::ViewLights, r.ViewLights),
					BufferWrite(c, ClusterAssignBindings::Bounds, r.Bounds), BufferWrite(c, ClusterAssignBindings::Indices, r.Indices),
					BufferWrite(c, ClusterAssignBindings::Stats, r.Stats) };
				Bind(c, program, label, writes);
				const std::array<std::uint32_t, 4> constants{ groupsX * ClusterAssignBindings::ThreadGroupSize, 0, 0, 0 };
				c.Commands().PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
				c.Commands().Dispatch(groupsX, groupsY, 1);
			}));

		// 4. Summary: occupancy words, records and statistics.
		const std::uint32_t summaryThreads = std::max(clusters, localCount);
		resources.Passes.push_back(graph.AddPass(
			desc.DebugName + " summary", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(r.Grid, S::ShaderRead);
				b.Read(lights.Header, S::ShaderRead);
				b.Read(r.ViewLights, S::ShaderRead);
				b.Write(r.Records, S::ShaderWrite);
				b.ReadWrite(r.Stats, S::ShaderRead | S::ShaderWrite);
				b.ReadWrite(r.Indices, S::ShaderRead | S::ShaderWrite);
			},
			[program = desc.Scan, label = desc.DebugName + " summary", r, header = lights.Header, summaryThreads](RenderCommandContext& c)
			{
				const std::array<Rhi::DescriptorWrite, 6> writes{ BufferWrite(c, ClusterScanBindings::Grid, r.Grid),
					BufferWrite(c, ClusterScanBindings::LightHeader, header), BufferWrite(c, ClusterScanBindings::ViewLights, r.ViewLights),
					BufferWrite(c, ClusterScanBindings::Records, r.Records), BufferWrite(c, ClusterScanBindings::Stats, r.Stats),
					BufferWrite(c, ClusterScanBindings::Indices, r.Indices) };
				Bind(c, program, label, writes);
				c.Commands().Dispatch(Groups(summaryThreads, ClusterScanBindings::ThreadGroupSize), 1, 1);
			}));
		return resources;
	}

	GraphTexture ClusteredLightAssigner::RecordHeatmap(RenderGraph& graph, const ClusterGraphResources& clusters, GraphTexture depth) const
	{
		if (!HasHeatmap())
		{
			throw std::logic_error(desc.DebugName + " has no heatmap program");
		}
		const auto depthDesc = graph.GetDesc(depth);
		const std::uint32_t width = clusters.GridRecord.Limits[0];
		const std::uint32_t height = clusters.GridRecord.Limits[1];
		const bool depthFormat = depthDesc.PixelFormat == Rhi::Format::D32Float;
		if (depthDesc.Dimension != Rhi::TextureDimension::Texture2D || depthDesc.Samples != Rhi::SampleCount::X1 ||
			(!depthFormat && depthDesc.PixelFormat != Rhi::Format::R32Float) || depthDesc.Extent.Width != width ||
			depthDesc.Extent.Height != height ||
			(static_cast<std::uint32_t>(depthDesc.Usage) & static_cast<std::uint32_t>(Rhi::TextureUsage::Sampled)) == 0)
		{
			throw std::invalid_argument(desc.DebugName + " heatmap needs a sampled viewport-sized D32Float or R32Float depth texture");
		}
		Rhi::TextureDesc heatmapDesc;
		heatmapDesc.Extent = { width, height, 1 };
		heatmapDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
		heatmapDesc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferSource;
		const std::string name = desc.DebugName + " heatmap";
		heatmapDesc.DebugName = name;
		const auto heatmap = graph.CreateTexture(heatmapDesc);
		graph.AddPass(
			name, Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(clusters.Grid, S::ShaderRead);
				b.Read(clusters.Records, S::ShaderRead);
				b.Read(depth, S::ShaderRead);
				b.Write(heatmap, S::ShaderWrite);
			},
			[program = desc.Heatmap, label = name, grid = clusters.Grid, records = clusters.Records, depth, heatmap, depthFormat, width,
				height](RenderCommandContext& c)
			{
				Rhi::TextureViewDesc depthView;
				depthView.PixelFormat = depthFormat ? Rhi::Format::D32Float : Rhi::Format::R32Float;
				depthView.Aspect = depthFormat ? Rhi::TextureAspect::Depth : Rhi::TextureAspect::Automatic;
				Rhi::TextureViewDesc outputView;
				outputView.PixelFormat = Rhi::Format::RGBA8Unorm;
				std::array<Rhi::DescriptorWrite, 4> writes{ BufferWrite(c, ClusterHeatmapBindings::Grid, grid),
					BufferWrite(c, ClusterHeatmapBindings::Records, records) };
				writes[2].Binding = ClusterHeatmapBindings::Depth;
				writes[2].TextureResource = &c.CreateView(depth, depthView);
				writes[3].Binding = ClusterHeatmapBindings::Output;
				writes[3].TextureResource = &c.CreateView(heatmap, outputView);
				Bind(c, program, label, writes);
				constexpr auto group = ClusterHeatmapBindings::ThreadGroupSize;
				c.Commands().Dispatch(Groups(width, group), Groups(height, group), 1);
			});
		return heatmap;
	}
} // namespace Swim::Render
