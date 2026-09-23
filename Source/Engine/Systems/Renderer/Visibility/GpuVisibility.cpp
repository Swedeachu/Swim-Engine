#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Visibility/GpuDrawRecord.h"
#include "Engine/Systems/Renderer/Visibility/GpuLodState.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityStats.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;
		using B = GpuVisibilityBindings;

		template <typename T> std::span<const std::byte> AsBytes(const std::vector<T>& values)
		{
			return std::as_bytes(std::span(values));
		}

		// Copies `bytes` (declared now) into a persistent buffer as one transfer pass.
		void RecordPersistentUpload(RenderGraph& graph, const std::string& label, GraphBuffer target, std::vector<std::byte> bytes)
		{
			const auto size = bytes.size();
			const auto staging = graph.CreateUpload(std::span<const std::byte>(bytes), label + " staging");
			graph.AddPass(
				label, Rhi::QueueType::Transfer,
				[&](RenderGraphBuilder& b)
				{
					b.Read(staging, S::CopySource);
					b.ReadWrite(target, S::CopyDestination);
				},
				[staging, target, size](RenderCommandContext& c)
				{
					const auto source = c.GetRange(staging);
					const auto destination = c.GetRange(target);
					c.Commands().CopyBuffer(*source.Buffer, *destination.Buffer, { source.Offset, destination.Offset, size });
				});
		}
	} // namespace

	GpuVisibility::GpuVisibility(Rhi::Device& device, GpuVisibilityDesc desc)
		: pipeline(desc.CullPipeline), layout(desc.Layout), space(desc.Space), maxObjects(desc.MaxObjects),
		  bins(desc.MaterialBinCapacities, desc.IndexPageSlots), materialBins(desc.MaxMaterialSets, 0u), name(std::move(desc.DebugName))
	{
		if (!pipeline || !layout || maxObjects == 0 || materialBins.empty())
		{
			throw std::invalid_argument(name + " needs the culling pipeline/layout, object capacity and a material-set table");
		}
		constexpr auto usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination;
		materialBinBuffer = device.CreateBuffer(
			{ materialBins.size() * sizeof(std::uint32_t), usage, Rhi::MemoryPreference::DeviceLocal, name + " material bins" });
		lodStateBuffer = device.CreateBuffer(
			{ std::uint64_t(maxObjects) * sizeof(GpuLodState), usage, Rhi::MemoryPreference::DeviceLocal, name + " LOD history" });
		occlusionHistoryBuffer = device.CreateBuffer(
			{ std::uint64_t(maxObjects) * sizeof(std::uint32_t), usage, Rhi::MemoryPreference::DeviceLocal, name + " occlusion history" });
		Rhi::TextureDesc nullDesc;
		nullDesc.Extent = { 1, 1, 1 };
		nullDesc.PixelFormat = Rhi::Format::R32Float;
		nullDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
		const auto nullName = name + " null HZB";
		nullDesc.DebugName = nullName;
		nullHzb = device.CreateTexture(nullDesc);
		if (!materialBinBuffer || !lodStateBuffer || !occlusionHistoryBuffer || !nullHzb)
		{
			throw std::runtime_error(name + " buffers could not be created");
		}
	}

	GpuVisibility::~GpuVisibility() = default;

	void GpuVisibility::SetMaterialBin(std::uint32_t materialSet, std::uint32_t materialBin)
	{
		if (materialSet >= materialBins.size() || materialBin >= bins.GetMaterialBins())
		{
			throw std::out_of_range(name + " material set or bin is out of range");
		}
		if (materialBins[materialSet] != materialBin)
		{
			materialBins[materialSet] = materialBin;
			materialBinsDirty = true;
		}
	}

	std::uint32_t GpuVisibility::GetMaterialBin(std::uint32_t materialSet) const
	{
		return materialSet < materialBins.size() ? materialBins[materialSet] : 0u;
	}

	GpuVisibility::PersistentImports& GpuVisibility::Import(RenderGraph& graph, std::uint64_t graphId)
	{
		// One import per graph: a frame's early and late phases share the history.
		if (imports.Graph == graphId)
		{
			return imports;
		}
		imports.Graph = graphId;
		imports.MaterialTable = graph.ImportBuffer(*materialBinBuffer, S::ShaderRead);
		imports.LodState = graph.ImportBuffer(*lodStateBuffer, S::ShaderRead);
		imports.OcclusionHistory = graph.ImportBuffer(*occlusionHistoryBuffer, S::ShaderRead);
		if (!historyInitialized)
		{
			RecordPersistentUpload(
				graph, name + " LOD history reset", imports.LodState, std::vector<std::byte>(lodStateBuffer->GetDesc().Size));
			RecordPersistentUpload(graph, name + " occlusion history reset", imports.OcclusionHistory,
				std::vector<std::byte>(occlusionHistoryBuffer->GetDesc().Size));
			imports.NullHzb = graph.ImportTexture(*nullHzb, S::Undefined);
			const float zero = 0.0f;
			AddTextureUpload(
				graph, name + " null HZB upload", std::as_bytes(std::span(&zero, 1)), imports.NullHzb, { 0, {}, {}, { 1, 1, 1 } });
			graph.Export(imports.NullHzb, S::ShaderRead);
			historyInitialized = true;
		}
		else
		{
			imports.NullHzb = graph.ImportTexture(*nullHzb, S::ShaderRead);
		}
		return imports;
	}

	VisibilityGraphResources GpuVisibility::Record(
		RenderGraph& graph, const GpuSceneGraphResources& scene, const GeometryGraphResources& geometry, const VisibilityFrameDesc& frame)
	{
		if (frame.IndexPages.size() > bins.GetPageSlots())
		{
			throw std::invalid_argument(name + " frame lists more index pages than page slots");
		}
		if (scene.RowCount > maxObjects)
		{
			throw std::length_error(name + " LOD history is smaller than the GPU Scene row count");
		}
		const bool late = frame.Phase == VisibilityPhase::Late;
		const bool forwardDepth = (frame.View.Flags & std::uint32_t(GpuViewFlags::ForwardDepth)) != 0;
		if (late && (!frame.Hzb || frame.Hzb->MipCount == 0 || (frame.Hzb->Convention == DepthConvention::Forward) != forwardDepth))
		{
			throw std::invalid_argument(name + " late phase needs this frame's HZB built with the view's depth convention");
		}
		if (frame.Phase != VisibilityPhase::Single && frame.Phase != VisibilityPhase::Early && !late)
		{
			throw std::invalid_argument(name + " unknown visibility phase");
		}

		VisibilityGraphResources resources;
		resources.Bins = &bins;
		resources.Phase = frame.Phase;
		const auto binCount = bins.GetBinCount();
		const auto capacity = bins.GetTotalCapacity();
		resources.Commands = graph.CreateBuffer({ std::uint64_t(capacity) * sizeof(Rhi::DrawIndexedIndirectCommand),
			Rhi::BufferUsage::Indirect | Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource |
				Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::DeviceLocal, name + " commands" });
		resources.DrawRecords = graph.CreateBuffer({ std::uint64_t(capacity) * sizeof(GpuDrawRecord),
			Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::DeviceLocal, name + " draw records" });
		resources.Counts = graph.CreateBuffer({ std::uint64_t(binCount) * sizeof(std::uint32_t),
			Rhi::BufferUsage::Indirect | Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination |
				Rhi::BufferUsage::TransferSource,
			Rhi::MemoryPreference::DeviceLocal, name + " counts" });
		resources.Stats = graph.CreateBuffer(
			{ sizeof(VisibilityStats), Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource,
				Rhi::MemoryPreference::DeviceLocal, name + " stats" });

		// Per-frame inputs.
		const auto view = graph.CreateUpload(std::as_bytes(std::span(&frame.View, 1)), name + " view", Rhi::BufferUsage::Storage, 16);
		const auto ranges = graph.CreateUpload(AsBytes(bins.GetRanges()), name + " bin ranges", Rhi::BufferUsage::Storage, 16);
		std::vector<std::uint32_t> pages(bins.GetPageSlots(), UINT32_MAX);
		std::copy(frame.IndexPages.begin(), frame.IndexPages.end(), pages.begin());
		const auto pageTable = graph.CreateUpload(AsBytes(pages), name + " index pages", Rhi::BufferUsage::Storage, 16);

		// Counters restart at zero every frame. The no-count fallback also zeroes every
		// command slot, so slots the cull leaves unwritten draw zero instances.
		const bool zeroCommands = frame.ZeroUnusedCommands;
		const std::uint64_t countBytes = std::uint64_t(binCount) * sizeof(std::uint32_t);
		const std::uint64_t commandBytes = zeroCommands ? std::uint64_t(capacity) * sizeof(Rhi::DrawIndexedIndirectCommand) : 0;
		const std::vector<std::byte> zeros(std::max<std::uint64_t>(countBytes + sizeof(VisibilityStats), commandBytes));
		const auto zeroUpload = graph.CreateUpload(std::span<const std::byte>(zeros), name + " zeros");
		graph.AddPass(
			name + " clear", Rhi::QueueType::Transfer,
			[&](RenderGraphBuilder& b)
			{
				b.Read(zeroUpload, S::CopySource);
				b.Write(resources.Counts, S::CopyDestination);
				b.Write(resources.Stats, S::CopyDestination);
				if (zeroCommands)
				{
					b.Write(resources.Commands, S::CopyDestination);
				}
			},
			[zeroUpload, counts = resources.Counts, stats = resources.Stats, commands = resources.Commands, countBytes, commandBytes](
				RenderCommandContext& c)
			{
				const auto source = c.GetRange(zeroUpload);
				c.Commands().CopyBuffer(*source.Buffer, c.Get(counts), { source.Offset, 0, countBytes });
				c.Commands().CopyBuffer(*source.Buffer, c.Get(stats), { source.Offset + countBytes, 0, sizeof(VisibilityStats) });
				if (commandBytes != 0)
				{
					c.Commands().CopyBuffer(*source.Buffer, c.Get(commands), { source.Offset, 0, commandBytes });
				}
			});

		// Persistent state: material-bin table when edited, histories once.
		const auto& persistent = Import(graph, scene.Instances.Graph);
		const auto materialTable = persistent.MaterialTable;
		const auto lodState = persistent.LodState;
		const auto occlusion = persistent.OcclusionHistory;
		const auto hzbTexture = late ? frame.Hzb->Pyramid : persistent.NullHzb;
		const auto hzbMips = late ? frame.Hzb->MipCount : 1u;
		if (materialBinsDirty)
		{
			std::vector<std::byte> bytes(materialBins.size() * sizeof(std::uint32_t));
			std::memcpy(bytes.data(), materialBins.data(), bytes.size());
			RecordPersistentUpload(graph, name + " material bins upload", materialTable, std::move(bytes));
		}

		const std::array<std::uint32_t, 8> constants{ scene.RowCount, binCount, static_cast<std::uint32_t>(materialBins.size()),
			bins.GetPageSlots(), static_cast<std::uint32_t>(frame.Phase), late ? frame.Hzb->Width : 0u, late ? frame.Hzb->Height : 0u,
			late ? frame.Hzb->MipCount : 0u };
		static_assert(sizeof(constants) == B::PushConstantBytes);
		const char* phaseName = frame.Phase == VisibilityPhase::Early ? " early cull" : late ? " late cull" : " cull";
		resources.CullPass = graph.AddPass(
			name + phaseName, Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(scene.Instances, S::ShaderRead);
				b.Read(scene.Transforms, S::ShaderRead);
				b.Read(geometry.Metadata, S::ShaderRead);
				b.Read(geometry.Submeshes, S::ShaderRead);
				b.Read(view, S::ShaderRead);
				b.Read(materialTable, S::ShaderRead);
				b.Read(ranges, S::ShaderRead);
				b.Read(pageTable, S::ShaderRead);
				b.ReadWrite(lodState, S::ShaderRead | S::ShaderWrite);
				if (zeroCommands)
				{
					b.ReadWrite(resources.Commands, S::ShaderRead | S::ShaderWrite); // Keeps the zeroed slots.
				}
				else
				{
					b.Write(resources.Commands, S::ShaderWrite);
				}
				b.Write(resources.DrawRecords, S::ShaderWrite);
				b.ReadWrite(resources.Counts, S::ShaderRead | S::ShaderWrite);
				b.ReadWrite(resources.Stats, S::ShaderRead | S::ShaderWrite);
				b.ReadWrite(occlusion, S::ShaderRead | S::ShaderWrite);
				b.Read(hzbTexture, S::ShaderRead);
			},
			[=, pipeline = pipeline, layout = layout, space = space, instances = scene.Instances, transforms = scene.Transforms,
				meshes = geometry.Metadata, submeshes = geometry.Submeshes, commands = resources.Commands, records = resources.DrawRecords,
				counts = resources.Counts, stats = resources.Stats, label = name](RenderCommandContext& c)
			{
				auto table = c.Device().CreateDescriptorTable({ layout, space, 0, label + " table" });
				if (!table)
				{
					throw std::runtime_error(label + " descriptor table could not be created");
				}
				std::array<Rhi::DescriptorWrite, B::Count> writes{};
				const auto whole = [&](std::uint32_t binding, GraphBuffer buffer)
				{
					writes[binding].Binding = binding;
					writes[binding].BufferResource = &c.Get(buffer);
				};
				const auto range = [&](std::uint32_t binding, GraphBuffer buffer)
				{
					const auto staged = c.GetRange(buffer);
					writes[binding].Binding = binding;
					writes[binding].BufferResource = staged.Buffer;
					writes[binding].BufferOffset = staged.Offset;
					writes[binding].BufferRange = staged.Size;
				};
				whole(B::Instances, instances);
				whole(B::Transforms, transforms);
				whole(B::Meshes, meshes);
				whole(B::Submeshes, submeshes);
				range(B::View, view);
				whole(B::MaterialBins, materialTable);
				range(B::BinRanges, ranges);
				range(B::IndexPages, pageTable);
				whole(B::LodState, lodState);
				whole(B::Commands, commands);
				whole(B::DrawRecords, records);
				whole(B::Counts, counts);
				whole(B::Stats, stats);
				whole(B::OcclusionHistory, occlusion);
				Rhi::TextureViewDesc hzbView;
				hzbView.PixelFormat = Rhi::Format::R32Float;
				hzbView.MipLevelCount = hzbMips;
				writes[B::Hzb].Binding = B::Hzb;
				writes[B::Hzb].TextureResource = &c.CreateView(hzbTexture, hzbView);
				table->Write(writes);
				auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
				auto& list = c.Commands();
				list.BindComputePipeline(*pipeline);
				list.BindDescriptorTable(space, retained);
				list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
				list.Dispatch((constants[0] + B::ThreadGroupSize - 1) / B::ThreadGroupSize, 1, 1);
			});
		if (frame.ReadStats)
		{
			resources.StatsReadback = AddBufferReadback(graph, name + " stats readback", resources.Stats, 0, sizeof(VisibilityStats));
		}
		materialBinsDirty = false;
		return resources;
	}
} // namespace Swim::Render
