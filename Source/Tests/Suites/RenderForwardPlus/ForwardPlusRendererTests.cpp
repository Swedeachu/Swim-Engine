#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRenderer.h"
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityBinLayout.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Fixtures/GpuSceneFixture.h"

#include <array>
#include <cstring>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace Scene = Swim::Testing::ClusterScene;

namespace
{
	class MockGraphicsPipeline final : public Rhi::GraphicsPipeline
	{
	  public:
		std::uintptr_t GetNativeHandle() const override { return 15; }
	};

	// A Forward+ frame on the mock device: every input is an imported buffer or
	// texture, so the passes' bindings can be compared with known objects.
	struct ForwardWorld
	{
		static constexpr std::uint32_t Slots = 2;
		static constexpr std::uint32_t OpaqueCapacity = 8;
		static constexpr std::uint32_t TransparentCapacity = 5;
		static constexpr std::uint32_t Width = 320;
		static constexpr std::uint32_t Height = 180;

		ForwardWorld() : bins(std::array<std::uint32_t, 2>{ OpaqueCapacity, TransparentCapacity }, Slots)
		{
			fixture.device.CreateTextures = true;
			using T = Rhi::DescriptorType;
			Rhi::DescriptorSchemaDesc draw{ 0, {} };
			for (std::uint32_t binding = 0; binding < ForwardPlusDrawBindings::Count; ++binding)
			{
				const auto type = binding == ForwardPlusDrawBindings::EnvironmentSampler ? T::Sampler
					: binding == ForwardPlusDrawBindings::EnvironmentPrefiltered ||
						binding == ForwardPlusDrawBindings::EnvironmentBrdfLut || binding == ForwardPlusDrawBindings::ShadowAtlas
					? T::SampledTexture
					: T::ReadOnlyStorageBuffer;
				draw.Bindings.push_back({ binding, type, 1, Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment });
			}
			const auto bindless = ForwardPlusBindlessSpace(16, 4);
			opaqueLayout.program.Interface.DescriptorSchemas = { draw, bindless };
			transparentLayout.program.Interface.DescriptorSchemas = { draw, bindless };
			Rhi::DescriptorSchemaDesc sort{ 0, {} };
			for (std::uint32_t binding = 0; binding < ForwardTransparentSortBindings::Count; ++binding)
			{
				sort.Bindings.push_back({ binding, T::StorageBuffer, 1, Rhi::ShaderStageMask::Compute });
			}
			sortLayout.program.Interface.DescriptorSchemas = { sort };
			bindlessTable = fixture.device.CreateDescriptorTable({ &opaqueLayout, 1, 0, "Bindless" });

			const auto buffer = [&](std::uint64_t size, Rhi::BufferUsage usage)
			{
				return fixture.device.CreateBuffer({ size, usage | Rhi::BufferUsage::Storage, Rhi::MemoryPreference::DeviceLocal, "Test" });
			};
			for (auto& page : pages)
			{
				page = buffer(4096, Rhi::BufferUsage::Index);
			}
			commands = buffer(std::uint64_t(bins.GetTotalCapacity()) * 20, Rhi::BufferUsage::Indirect);
			counts = buffer(std::uint64_t(bins.GetBinCount()) * 4, Rhi::BufferUsage::Indirect);
			drawRecords = buffer(std::uint64_t(bins.GetTotalCapacity()) * 8, Rhi::BufferUsage::None);
			materials = buffer(4 * 80, Rhi::BufferUsage::None);
			lightRows = buffer(8 * 64, Rhi::BufferUsage::None);
			lightHeader = buffer(32, Rhi::BufferUsage::None);
			grid = buffer(sizeof(ClusterGridRecord), Rhi::BufferUsage::None);
			records = buffer(1024 * 16, Rhi::BufferUsage::None);
			indices = buffer(4096, Rhi::BufferUsage::None);
			irradiance = buffer(144, Rhi::BufferUsage::None);
			shadowRecords = buffer(4 * 64, Rhi::BufferUsage::None);
			shadowViews = buffer(4 * 96, Rhi::BufferUsage::None);
			Rhi::TextureDesc shadowAtlasDesc;
			shadowAtlasDesc.Extent = { 256, 256, 1 };
			shadowAtlasDesc.PixelFormat = Rhi::Format::D32Float;
			shadowAtlasDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::DepthStencilAttachment;
			shadowAtlas = fixture.device.CreateTexture(shadowAtlasDesc);
			instances = buffer(16 * 64, Rhi::BufferUsage::None);
			transforms = buffer(16 * 96, Rhi::BufferUsage::None);
			Rhi::TextureDesc cube;
			cube.Dimension = Rhi::TextureDimension::TextureCube;
			cube.Extent = { 16, 16, 1 };
			cube.PixelFormat = Rhi::Format::RGBA16Float;
			cube.Usage = Rhi::TextureUsage::Sampled;
			cube.MipLevels = 5;
			cube.ArrayLayers = 6;
			prefiltered = fixture.device.CreateTexture(cube);
			Rhi::TextureDesc lut;
			lut.Extent = { 32, 32, 1 };
			lut.PixelFormat = Rhi::Format::RGBA16Float;
			lut.Usage = Rhi::TextureUsage::Sampled;
			brdfLut = fixture.device.CreateTexture(lut);

			ClusterGridDesc gridDesc;
			gridDesc.ViewportWidth = Width;
			gridDesc.ViewportHeight = Height;
			gridDesc.TileSize = 32;
			gridDesc.SliceCount = 8;
			gridDesc.Far = 50.0f;
			clusters.GridRecord = MakeClusterGridRecord(gridDesc, Scene::Camera(float(Width) / float(Height)));
			clusters.Layout = ComputeClusterGridLayout(gridDesc);
		}

		ForwardPlusRendererDesc Desc(VisibilityDrawPath path = VisibilityDrawPath::IndirectCount)
		{
			ForwardPlusRendererDesc desc;
			desc.Opaque = { &opaquePipeline, &opaqueLayout };
			desc.Transparent = { &transparentPipeline, &transparentLayout };
			desc.SortPipeline = &sortPipeline;
			desc.SortLayout = &sortLayout;
			desc.DrawPath = path;
			return desc;
		}

		// Imports everything into `graph` and fills `frame` (pointers into this world).
		void Import(RenderGraph& graph, ForwardPlusFrame& frame, bool environment)
		{
			const auto in = [&](std::unique_ptr<Rhi::Buffer>& buffer)
			{
				return graph.ImportBuffer(*buffer, Rhi::ResourceState::ShaderRead);
			};
			scene = {};
			scene.Instances = in(instances);
			scene.Transforms = in(transforms);
			geometry = {};
			for (auto& page : pages)
			{
				geometry.Pages.push_back(in(page));
			}
			visibility = {};
			visibility.Commands = in(commands);
			visibility.Counts = in(counts);
			visibility.DrawRecords = in(drawRecords);
			visibility.Bins = &bins;
			materialResources = { in(materials), std::nullopt, 4, 80 };
			lights = {};
			lights.Lights = in(lightRows);
			lights.Header = in(lightHeader);
			clusters.Grid = in(grid);
			clusters.Records = in(records);
			clusters.Indices = in(indices);
			frame = {};
			frame.Scene = &scene;
			frame.Geometry = &geometry;
			frame.Visibility = &visibility;
			frame.PageSlots = { { 1, 0 }, { 3, 2 } }; // (index, vertex) pages.
			frame.Materials = &materialResources;
			frame.Bindless = bindlessTable.get();
			frame.Lights = &lights;
			frame.Clusters = &clusters;
			frame.View.CameraPosition = { 0, 4, 18 };
			frame.View.CameraForward = { 0, -2, -18 };
			if (environment)
			{
				environmentResources = {};
				environmentResources.Irradiance = in(irradiance);
				environmentResources.Prefiltered = graph.ImportTexture(*prefiltered, Rhi::ResourceState::ShaderRead);
				environmentResources.PrefilteredSize = 16;
				environmentResources.PrefilteredMipCount = 5;
				frame.Environment = &environmentResources;
				frame.BrdfLut = graph.ImportTexture(*brdfLut, Rhi::ResourceState::ShaderRead);
				shadowResources = {};
				shadowResources.Atlas = graph.ImportTexture(*shadowAtlas, Rhi::ResourceState::ShaderRead);
				shadowResources.Records = in(shadowRecords);
				shadowResources.Views = in(shadowViews);
				shadowResources.AtlasSize = 256;
				frame.Shadows = &shadowResources;
			}
		}

		ForwardPlusTargets Targets(
			RenderGraph& graph, std::uint32_t width = Width, Rhi::Format idFormat = ForwardPlusRenderer::ObjectIdFormat)
		{
			const auto texture = [&](Rhi::Format format, Rhi::TextureUsage usage)
			{
				Rhi::TextureDesc desc;
				desc.Extent = { width, Height, 1 };
				desc.PixelFormat = format;
				desc.Usage = usage | Rhi::TextureUsage::TransferSource;
				return graph.CreateTexture(desc);
			};
			ForwardPlusTargets targets;
			targets.Color = texture(ForwardPlusRenderer::ColorFormat, Rhi::TextureUsage::ColorAttachment);
			targets.ObjectId = texture(idFormat, Rhi::TextureUsage::ColorAttachment);
			targets.Depth = texture(CanonicalDepthFormat, Rhi::TextureUsage::DepthStencilAttachment);
			return targets;
		}

		std::vector<Testing::MockCommand> Commands(const std::string& kind) const
		{
			std::vector<Testing::MockCommand> result;
			for (const auto& command : *fixture.device.Commands)
			{
				if (command.Kind == kind)
				{
					result.push_back(command);
				}
			}
			return result;
		}

		Testing::GpuSceneFixture fixture;
		VisibilityBinLayout bins;
		Testing::MockPipelineLayout opaqueLayout, transparentLayout, sortLayout;
		MockGraphicsPipeline opaquePipeline, transparentPipeline;
		Testing::MockComputePipeline sortPipeline;
		std::unique_ptr<Rhi::DescriptorTable> bindlessTable;
		std::array<std::unique_ptr<Rhi::Buffer>, 4> pages;
		std::unique_ptr<Rhi::Buffer> commands, counts, drawRecords, materials, lightRows, lightHeader, grid, records, indices, irradiance,
			shadowRecords, shadowViews, instances, transforms;
		std::unique_ptr<Rhi::Texture> prefiltered, brdfLut;
		GpuSceneGraphResources scene;
		GeometryGraphResources geometry;
		VisibilityGraphResources visibility;
		GpuMaterialGraphResources materialResources;
		GpuLightGraphResources lights;
		ClusterGraphResources clusters;
		EnvironmentGraphResources environmentResources;
		ShadowGraphResources shadowResources;
		std::unique_ptr<Rhi::Texture> shadowAtlas;
	};
} // namespace

SWIM_TEST("Render.ForwardPlusRenderer", "PipelineStatesFollowTheBinContract")
{
	Testing::MockPipelineLayout layout;
	Testing::MockShaderProgram program;
	const auto opaque = ForwardPlusRenderer::PipelineDesc(ForwardPlusBin::Opaque, program, layout);
	SWIM_REQUIRE_EQUAL(opaque.ColorFormats.size(), std::size_t(3));
	SWIM_CHECK(opaque.ColorFormats[0] == Rhi::Format::RGBA16Float);
	SWIM_CHECK(opaque.ColorFormats[1] == Rhi::Format::R32Float);
	SWIM_CHECK(opaque.ColorFormats[2] == Rhi::Format::RG16Float); // Motion vectors (item 75).
	SWIM_REQUIRE_EQUAL(opaque.BlendAttachments.size(), std::size_t(3));
	SWIM_CHECK(!opaque.BlendAttachments[0].Enabled && !opaque.BlendAttachments[1].Enabled && !opaque.BlendAttachments[2].Enabled);
	SWIM_CHECK(opaque.DepthStencilFormat == Rhi::Format::D32Float);
	SWIM_CHECK(opaque.DepthStencil.DepthTest && opaque.DepthStencil.DepthWrite);
	SWIM_CHECK(opaque.DepthStencil.DepthCompare == Rhi::CompareOp::GreaterEqual); // Reverse-Z.
	SWIM_CHECK(opaque.Raster.Cull == Rhi::CullMode::None);
	SWIM_CHECK(opaque.Raster.Winding == Rhi::FrontFace::CounterClockwise);
	SWIM_CHECK(opaque.Program == &program && opaque.Layout == &layout);

	const auto transparent = ForwardPlusRenderer::PipelineDesc(ForwardPlusBin::Transparent, program, layout);
	SWIM_REQUIRE_EQUAL(transparent.ColorFormats.size(), std::size_t(1));
	SWIM_REQUIRE_EQUAL(transparent.BlendAttachments.size(), std::size_t(1));
	const auto& blend = transparent.BlendAttachments[0];
	SWIM_CHECK(blend.Enabled);
	SWIM_CHECK(blend.SourceColor == Rhi::BlendFactor::One && blend.DestinationColor == Rhi::BlendFactor::OneMinusSourceAlpha);
	SWIM_CHECK(blend.SourceAlpha == Rhi::BlendFactor::One && blend.DestinationAlpha == Rhi::BlendFactor::OneMinusSourceAlpha);
	SWIM_CHECK(blend.ColorOperation == Rhi::BlendOp::Add && blend.AlphaOperation == Rhi::BlendOp::Add);
	SWIM_CHECK(transparent.DepthStencil.DepthTest && !transparent.DepthStencil.DepthWrite);
	SWIM_CHECK(transparent.DepthStencil.DepthCompare == Rhi::CompareOp::GreaterEqual);
	SWIM_CHECK_THROWS(ForwardPlusRenderer::PipelineDesc(static_cast<ForwardPlusBin>(7), program, layout), std::invalid_argument);

	const auto capacities = ForwardPlusRenderer::VisibilityBinCapacities(1000, 64);
	SWIM_CHECK((capacities == std::vector<std::uint32_t>{ 1000, 64 }));
	SWIM_CHECK_THROWS(ForwardPlusRenderer::VisibilityBinCapacities(0, 64), std::invalid_argument);
	SWIM_CHECK_THROWS(ForwardPlusRenderer::VisibilityBinCapacities(8, ForwardTransparentSortBindings::MaxDraws + 1), std::invalid_argument);
}

SWIM_TEST("Render.ForwardPlusRenderer", "RecordsOpaqueSortAndTransparentPassesPerPageSlot")
{
	ForwardWorld world;
	SWIM_CHECK_THROWS(ForwardPlusRenderer(world.fixture.device, ForwardPlusRendererDesc{}), std::invalid_argument);
	const ForwardPlusRenderer renderer(world.fixture.device, world.Desc());

	RenderGraph graph;
	ForwardPlusFrame frame;
	world.Import(graph, frame, true);
	const auto targets = world.Targets(graph);
	const auto resources = renderer.Record(graph, frame, targets);
	graph.Export(targets.Color, Rhi::ResourceState::ColorAttachment);
	SWIM_CHECK(!resources.EnvironmentFallback);
	SWIM_CHECK_EQUAL(resources.TransparentCapacity, ForwardWorld::TransparentCapacity);
	SWIM_CHECK_EQUAL(resources.SortSize, 8u);
	SWIM_CHECK_EQUAL(resources.ViewRecord.Flags, ForwardViewFlagEnvironment | ForwardViewFlagShadows);
	SWIM_CHECK(!resources.ShadowFallback);
	SWIM_CHECK_EQUAL(resources.ViewRecord.PrefilteredMipCount, 5u);
	SWIM_CHECK_EQUAL(resources.ViewRecord.MaterialCount, 4u);
	SWIM_CHECK_EQUAL(graph.GetDesc(resources.SortedCommands).Size, std::uint64_t(2 * 5 * 20));
	SWIM_CHECK_EQUAL(graph.GetDesc(resources.SortedCounts).Size, std::uint64_t(2 * 4));
	SWIM_CHECK_EQUAL(graph.GetDesc(resources.SortScratch).Size, std::uint64_t(2 * 8 * 16));
	// No velocity target: a transient RG16Float attachment of the grid's size stands in.
	const auto& velocity = graph.GetDesc(resources.Velocity);
	SWIM_CHECK(velocity.PixelFormat == ForwardPlusRenderer::VelocityFormat);
	SWIM_CHECK_EQUAL(velocity.Extent.Width, ForwardWorld::Width);
	SWIM_CHECK_EQUAL(velocity.Extent.Height, ForwardWorld::Height);
	world.fixture.device.Commands->clear();
	world.fixture.executor->Execute(graph.Compile());
	world.fixture.executor->Wait();

	// Opaque: one count draw per page slot over the visibility commands.
	const auto draws = world.Commands("DrawIndexedIndirectCount");
	SWIM_REQUIRE_EQUAL(draws.size(), std::size_t(4));
	for (std::uint32_t slot = 0; slot < 2; ++slot)
	{
		const auto bin = world.bins.GetBin(0, slot);
		SWIM_CHECK(draws[slot].Source == world.commands.get());
		SWIM_CHECK(draws[slot].Destination == world.counts.get());
		SWIM_CHECK_EQUAL(draws[slot].SourceOffset, std::uint64_t(world.bins.GetRange(bin).First) * 20);
		SWIM_CHECK_EQUAL(draws[slot].DestinationOffset, std::uint64_t(bin) * 4);
		SWIM_CHECK_EQUAL(draws[slot].Size, std::uint64_t(ForwardWorld::OpaqueCapacity));
	}
	// Transparent: the sorted commands, per slot.
	for (std::uint32_t slot = 0; slot < 2; ++slot)
	{
		const auto& draw = draws[2 + slot];
		SWIM_CHECK(draw.Source != world.commands.get() && draw.Source == draws[2].Source);
		SWIM_CHECK(draw.Destination != world.counts.get() && draw.Destination == draws[2].Destination);
		SWIM_CHECK_EQUAL(draw.SourceOffset, std::uint64_t(slot) * 5 * 20);
		SWIM_CHECK_EQUAL(draw.DestinationOffset, std::uint64_t(slot) * 4);
		SWIM_CHECK_EQUAL(draw.Size, std::uint64_t(5));
	}
	// Sort: one group per slot, pushing (first bin, first command, capacity, sort size).
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(1));
	SWIM_CHECK_EQUAL(dispatches[0].SourceOffset, 2u);
	SWIM_CHECK_EQUAL(dispatches[0].DestinationOffset, 1u);
	const auto pushes = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(pushes.size(), std::size_t(1));
	std::array<std::uint32_t, 4> constants{};
	std::memcpy(constants.data(), pushes[0].Data.data(), sizeof(constants));
	SWIM_CHECK_EQUAL(constants[0], world.bins.GetBin(1, 0));
	SWIM_CHECK_EQUAL(constants[1], world.bins.GetRange(world.bins.GetBin(1, 0)).First);
	SWIM_CHECK_EQUAL(constants[2], 5u);
	SWIM_CHECK_EQUAL(constants[3], 8u);
	SWIM_CHECK(world.Commands("CopyBufferToTexture").empty()); // The environment and shadows were supplied.

	// The last table is the transparent pass's slot 1: its vertex page and the shared inputs.
	const auto* table = world.fixture.device.LastDescriptorTable;
	SWIM_REQUIRE(table != nullptr);
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::Vertices, 0) == world.pages[2].get());
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::Materials, 0) == world.materials.get());
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::ClusterIndices, 0) == world.indices.get());
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::EnvironmentIrradiance, 0) == world.irradiance.get());
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::EnvironmentSampler, 0) == &renderer.GetEnvironmentSampler());
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::EnvironmentPrefiltered, 0) != nullptr);
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::ShadowRecords, 0) == world.shadowRecords.get());
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::ShadowViews, 0) == world.shadowViews.get());
	SWIM_CHECK(table->Element(ForwardPlusDrawBindings::ShadowAtlas, 0) != nullptr);
	SWIM_CHECK_EQUAL(table->ElementWrites, ForwardPlusDrawBindings::Count);
}

SWIM_TEST("Render.ForwardPlusRenderer", "FallbacksForMissingEnvironmentAndIndirectCount")
{
	ForwardWorld world;
	const ForwardPlusRenderer renderer(world.fixture.device, world.Desc(VisibilityDrawPath::ZeroFilledIndirect));
	RenderGraph graph;
	ForwardPlusFrame frame;
	world.Import(graph, frame, false);
	frame.View.DebugMode = ForwardPlusDebugMode::ClusterHeatmap;
	frame.View.Jitter = { 0.001f, -0.002f };
	auto targets = world.Targets(graph);
	Rhi::TextureDesc velocityDesc;
	velocityDesc.Extent = { ForwardWorld::Width, ForwardWorld::Height, 1 };
	velocityDesc.PixelFormat = ForwardPlusRenderer::VelocityFormat;
	velocityDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled;
	targets.Velocity = graph.CreateTexture(velocityDesc);
	const auto resources = renderer.Record(graph, frame, targets);
	SWIM_CHECK(resources.Velocity == *targets.Velocity); // A supplied velocity target is used as is.
	SWIM_CHECK(resources.ViewRecord.Jitter[0] == 0.001f && resources.ViewRecord.Jitter[1] == -0.002f);
	graph.Export(targets.Color, Rhi::ResourceState::ColorAttachment);
	SWIM_CHECK(resources.EnvironmentFallback);
	SWIM_CHECK(resources.ShadowFallback);
	SWIM_CHECK_EQUAL(resources.ViewRecord.Flags, 0u);
	SWIM_CHECK_EQUAL(resources.ViewRecord.PrefilteredMipCount, 1u);
	SWIM_CHECK_EQUAL(resources.ViewRecord.DebugMode, 1u);
	world.fixture.device.Commands->clear();
	world.fixture.executor->Execute(graph.Compile());
	world.fixture.executor->Wait();
	SWIM_CHECK(world.Commands("DrawIndexedIndirectCount").empty());
	const auto draws = world.Commands("DrawIndexedIndirect");
	SWIM_REQUIRE_EQUAL(draws.size(), std::size_t(4));
	SWIM_CHECK_EQUAL(draws[1].SourceOffset, std::uint64_t(world.bins.GetRange(world.bins.GetBin(0, 1)).First) * 20);
	SWIM_CHECK_EQUAL(draws[1].Size, std::uint64_t(ForwardWorld::OpaqueCapacity));
	SWIM_CHECK_EQUAL(draws[3].SourceOffset, std::uint64_t(5 * 20));
	SWIM_CHECK_EQUAL(draws[3].Size, std::uint64_t(5)); // The sort zero-fills the unused commands.
	// Six cube faces, the LUT and the shadow-atlas stand-ins are initialized.
	SWIM_CHECK_EQUAL(world.Commands("CopyBufferToTexture").size(), std::size_t(8));
}

SWIM_TEST("Render.ForwardPlusRenderer", "RejectsIncompleteFramesAndMismatchedTargets")
{
	ForwardWorld world;
	const ForwardPlusRenderer renderer(world.fixture.device, world.Desc());
	const auto attempt = [&](const auto& edit)
	{
		RenderGraph graph;
		ForwardPlusFrame frame;
		world.Import(graph, frame, true);
		auto targets = world.Targets(graph);
		edit(graph, frame, targets);
		renderer.Record(graph, frame, targets);
	};
	// The unmodified frame records.
	attempt(
		[](RenderGraph&, ForwardPlusFrame&, ForwardPlusTargets&)
		{
		});
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ForwardPlusFrame& frame, ForwardPlusTargets&)
						  {
							  frame.Clusters = nullptr;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ForwardPlusFrame& frame, ForwardPlusTargets&)
						  {
							  frame.Bindless = nullptr;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ForwardPlusFrame& frame, ForwardPlusTargets&)
						  {
							  frame.BrdfLut.reset();
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ForwardPlusFrame& frame, ForwardPlusTargets&)
						  {
							  frame.PageSlots.pop_back();
						  }),
		std::invalid_argument); // Bins cover two slots.
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ForwardPlusFrame& frame, ForwardPlusTargets&)
						  {
							  frame.PageSlots[1].VertexPage = 9;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ForwardPlusFrame& frame, ForwardPlusTargets&)
						  {
							  frame.View.CameraForward = { 0, 0, 0 };
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& graph, ForwardPlusFrame&, ForwardPlusTargets& targets)
						  {
							  targets = world.Targets(graph, 300); // Not the grid's viewport.
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph& graph, ForwardPlusFrame&, ForwardPlusTargets& targets)
						  {
							  targets = world.Targets(graph, ForwardWorld::Width, Rhi::Format::R32Uint);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ForwardPlusFrame&, ForwardPlusTargets& targets)
						  {
							  std::swap(targets.Color, targets.Depth);
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](RenderGraph&, ForwardPlusFrame&, ForwardPlusTargets& targets)
						  {
							  targets.Velocity = targets.ObjectId; // R32Float, not the velocity format.
						  }),
		std::invalid_argument);

	// Three material bins are not the Forward+ contract.
	const VisibilityBinLayout three(std::array<std::uint32_t, 3>{ 4, 4, 4 }, 2);
	SWIM_CHECK_THROWS(attempt(
						  [&](RenderGraph&, ForwardPlusFrame& frame, ForwardPlusTargets&)
						  {
							  const_cast<VisibilityGraphResources*>(frame.Visibility)->Bins = &three;
						  }),
		std::invalid_argument);
}
