#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/Shadows/ShadowRenderer.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Fixtures/GpuSceneFixture.h"

#include <array>
#include <cstring>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;
namespace Sh = Swim::Render::Shadows;

namespace
{
	class MockGraphicsPipeline final : public Rhi::GraphicsPipeline
	{
	  public:
		std::uintptr_t GetNativeHandle() const override { return 16; }
	};

	void DeclareCullingInterface(Testing::MockPipelineLayout& layout)
	{
		using B = GpuVisibilityBindings;
		Rhi::DescriptorSchemaDesc schema{ 0, {} };
		for (std::uint32_t binding = B::Instances; binding < B::Count; ++binding)
		{
			const bool writable = binding >= B::LodState;
			const auto type = binding == B::Hzb ? Rhi::DescriptorType::SampledTexture
				: writable						? Rhi::DescriptorType::StorageBuffer
												: Rhi::DescriptorType::ReadOnlyStorageBuffer;
			schema.Bindings.push_back({ binding, type, 1, Rhi::ShaderStageMask::Compute });
		}
		layout.program.Interface.DescriptorSchemas = { schema };
	}

	ShadowCasterDesc Caster(LightType type, std::uint32_t slot)
	{
		LightDesc desc;
		desc.Type = type;
		desc.Position = { 0, 5, 0 };
		desc.Direction = { 0.2f, -1, 0.1f };
		desc.OuterConeAngle = 0.6f;
		desc.ShadowIndex = slot;
		desc.Flags = LightFlags::CastsShadows;
		return { slot, Lights::EncodeLight(desc), float(slot), 0 };
	}

	// A shadow frame on the mock device: imported scene, geometry and material
	// buffers, a real GpuVisibility (mock cull pipeline) and a planned atlas.
	struct ShadowWorld
	{
		static constexpr std::uint32_t OpaqueCapacity = 8;
		static constexpr std::uint32_t MaskedCapacity = 4;

		ShadowWorld() : fixture(16)
		{
			fixture.device.CreateTextures = true;
			DeclareCullingInterface(cullLayout);
			Rhi::DescriptorSchemaDesc draw{ 0, {} };
			for (std::uint32_t binding = 0; binding < ShadowDepthBindings::Count; ++binding)
			{
				draw.Bindings.push_back({ binding, Rhi::DescriptorType::ReadOnlyStorageBuffer, 1, Rhi::ShaderStageMask::Vertex });
			}
			opaqueLayout.program.Interface.DescriptorSchemas = { draw };
			opaqueLayout.program.Interface.PushConstants = pushRanges;
			maskedLayout.program.Interface.DescriptorSchemas = { draw, ShadowBindlessSpace(16, 4) };
			maskedLayout.program.Interface.PushConstants = pushRanges;
			bindlessTable = fixture.device.CreateDescriptorTable({ &maskedLayout, ShadowDepthBindings::BindlessSpace, 0, "Bindless" });

			const auto buffer = [&](std::uint64_t size)
			{
				return fixture.device.CreateBuffer(
					{ size, Rhi::BufferUsage::Storage | Rhi::BufferUsage::Index, Rhi::MemoryPreference::DeviceLocal, "Test" });
			};
			for (auto& page : pages)
			{
				page = buffer(4096);
			}
			instances = buffer(16 * 64);
			transforms = buffer(16 * 96);
			metadata = buffer(4096);
			submeshes = buffer(4096);
			materials = buffer(4 * 80);
			visibility = MakeVisibility(ShadowRenderer::VisibilityBinCapacities(OpaqueCapacity, MaskedCapacity));

			settings.AtlasSize = 2048;
			settings.MinTile = 128;
			atlas = std::make_unique<ShadowAtlasAllocator>(settings.AtlasSize, settings.MinTile);
			camera.View = Testing::ClusterScene::LookAt({ 0, 4, 10 }, { 0, 0, 0 }, { 0, 1, 0 });
		}

		std::unique_ptr<GpuVisibility> MakeVisibility(std::vector<std::uint32_t> capacities, std::uint32_t pageSlots = 2)
		{
			GpuVisibilityDesc desc;
			desc.CullPipeline = &cullPipeline;
			desc.Layout = &cullLayout;
			desc.MaxObjects = 16;
			desc.MaxMaterialSets = 8;
			desc.MaterialBinCapacities = std::move(capacities);
			desc.IndexPageSlots = pageSlots;
			desc.DebugName = "Shadow visibility";
			return std::make_unique<GpuVisibility>(fixture.device, std::move(desc));
		}

		ShadowRendererDesc Desc(VisibilityDrawPath path = VisibilityDrawPath::IndirectCount)
		{
			ShadowRendererDesc desc;
			desc.Opaque = { &opaquePipeline, &opaqueLayout };
			desc.Masked = { &maskedPipeline, &maskedLayout };
			desc.DrawPath = path;
			return desc;
		}

		void Plan(const std::vector<ShadowCasterDesc>& casters) { plan = PlanShadows(settings, camera, casters, *atlas); }

		ShadowFrame Import(RenderGraph& graph)
		{
			const auto in = [&](std::unique_ptr<Rhi::Buffer>& buffer)
			{
				return graph.ImportBuffer(*buffer, Rhi::ResourceState::ShaderRead);
			};
			scene = {};
			scene.Instances = in(instances);
			scene.Transforms = in(transforms);
			scene.RowCount = 3;
			geometry = {};
			for (auto& page : pages)
			{
				geometry.Pages.push_back(in(page));
			}
			geometry.Metadata = in(metadata);
			geometry.Submeshes = in(submeshes);
			materialResources = { in(materials), std::nullopt, 4, 80 };
			ShadowFrame frame;
			frame.Scene = &scene;
			frame.Geometry = &geometry;
			frame.Visibility = visibility.get();
			frame.PageSlots = { { 1, 0 }, { 3, 2 } }; // (index, vertex) pages.
			frame.Materials = &materialResources;
			frame.Bindless = bindlessTable.get();
			frame.Plan = &plan;
			return frame;
		}

		void Execute(RenderGraph& graph)
		{
			fixture.device.Commands->clear();
			fixture.executor->Execute(graph.Compile());
			fixture.executor->Wait();
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
		Testing::MockPipelineLayout cullLayout, opaqueLayout, maskedLayout;
		Testing::MockComputePipeline cullPipeline;
		MockGraphicsPipeline opaquePipeline, maskedPipeline;
		const std::vector<Rhi::PushConstantRange> pushRanges{ { 0, ShadowDepthBindings::PushConstantBytes,
			Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment } };
		std::unique_ptr<Rhi::DescriptorTable> bindlessTable;
		std::array<std::unique_ptr<Rhi::Buffer>, 4> pages;
		std::unique_ptr<Rhi::Buffer> instances, transforms, metadata, submeshes, materials;
		std::unique_ptr<GpuVisibility> visibility;
		ShadowSettings settings;
		std::unique_ptr<ShadowAtlasAllocator> atlas;
		Sh::ShadowCamera camera;
		ShadowPlan plan;
		GpuSceneGraphResources scene;
		GeometryGraphResources geometry;
		GpuMaterialGraphResources materialResources;
	};
} // namespace

SWIM_TEST("Render.ShadowRenderer", "PipelineStateAndMaterialRouting")
{
	Testing::MockPipelineLayout layout;
	Testing::MockShaderProgram program;
	const auto pipeline = ShadowRenderer::PipelineDesc(program, layout);
	SWIM_CHECK(pipeline.ColorFormats.empty() && pipeline.BlendAttachments.empty());
	SWIM_CHECK(pipeline.DepthStencilFormat == Rhi::Format::D32Float);
	SWIM_CHECK(pipeline.DepthStencil.DepthTest && pipeline.DepthStencil.DepthWrite);
	SWIM_CHECK(pipeline.DepthStencil.DepthCompare == Rhi::CompareOp::GreaterEqual); // Reverse-Z.
	SWIM_CHECK(pipeline.Raster.Cull == Rhi::CullMode::None);
	SWIM_CHECK(pipeline.Program == &program && pipeline.Layout == &layout);

	SWIM_CHECK((ShadowRenderer::VisibilityBinCapacities(100, 20) == std::vector<std::uint32_t>{ 100, 20, 1 }));
	SWIM_CHECK_THROWS(ShadowRenderer::VisibilityBinCapacities(0, 20), std::invalid_argument);
	SWIM_CHECK_THROWS(ShadowRenderer::VisibilityBinCapacities(8, 0), std::invalid_argument);

	StandardPbr::Parameters opaque;
	auto masked = opaque;
	masked.Flags = StandardPbr::FlagAlphaMask;
	auto blended = opaque;
	blended.Flags = StandardPbr::FlagAlphaBlend;
	SWIM_CHECK(ShadowRenderer::MaterialBin(opaque) == ShadowBin::Opaque);
	SWIM_CHECK(ShadowRenderer::MaterialBin(masked) == ShadowBin::Masked);
	SWIM_CHECK(ShadowRenderer::MaterialBin(blended) == ShadowBin::Excluded);

	ShadowWorld world;
	ShadowRenderer::RouteMaterial(*world.visibility, 2, masked);
	ShadowRenderer::RouteMaterial(*world.visibility, 3, blended);
	ShadowRenderer::RouteMaterial(*world.visibility, 4, opaque);
	SWIM_CHECK_EQUAL(world.visibility->GetMaterialBin(2), 1u);
	SWIM_CHECK_EQUAL(world.visibility->GetMaterialBin(3), 2u);
	SWIM_CHECK_EQUAL(world.visibility->GetMaterialBin(4), 0u);
}

SWIM_TEST("Render.ShadowRenderer", "CullsEveryViewThenDrawsBothVariantsIntoEachTile")
{
	ShadowWorld world;
	SWIM_CHECK_THROWS(ShadowRenderer(ShadowRendererDesc{}), std::invalid_argument);
	auto noMasked = world.Desc();
	noMasked.Masked = {};
	SWIM_CHECK_THROWS(ShadowRenderer(noMasked), std::invalid_argument);
	const ShadowRenderer renderer(world.Desc());
	world.Plan({ Caster(LightType::Spot, 4), Caster(LightType::Point, 1) });
	SWIM_REQUIRE_EQUAL(world.plan.Views.size(), std::size_t(7));

	RenderGraph graph;
	const auto frame = world.Import(graph);
	const auto resources = renderer.Record(graph, frame);
	graph.Export(resources.Atlas, Rhi::ResourceState::ShaderRead);
	SWIM_CHECK_EQUAL(resources.AtlasSize, 2048u);
	SWIM_CHECK_EQUAL(resources.ViewCount, 7u);
	SWIM_CHECK_EQUAL(resources.RecordCount, 64u);
	SWIM_CHECK_EQUAL(resources.Visibility.size(), std::size_t(7));
	const auto& atlasDesc = graph.GetDesc(resources.Atlas);
	SWIM_CHECK(atlasDesc.PixelFormat == Rhi::Format::D32Float && atlasDesc.Extent.Width == 2048 && atlasDesc.Extent.Height == 2048);
	const auto usage = static_cast<std::uint32_t>(atlasDesc.Usage);
	SWIM_CHECK((usage & static_cast<std::uint32_t>(Rhi::TextureUsage::DepthStencilAttachment)) != 0u);
	SWIM_CHECK((usage & static_cast<std::uint32_t>(Rhi::TextureUsage::Sampled)) != 0u);
	SWIM_CHECK_EQUAL(graph.GetDesc(resources.Records).Size, std::uint64_t(64 * sizeof(GpuShadowRecord)));
	SWIM_CHECK_EQUAL(graph.GetDesc(resources.Views).Size, std::uint64_t(7 * sizeof(GpuShadowView)));
	world.Execute(graph);

	// One cull per view.
	SWIM_CHECK_EQUAL(world.Commands("Dispatch").size(), std::size_t(7));
	const auto& bins = world.visibility->GetBins();
	const auto draws = world.Commands("DrawIndexedIndirectCount");
	const auto viewports = world.Commands("SetViewport");
	const auto scissors = world.Commands("SetScissor");
	const auto indexBuffers = world.Commands("BindIndexBuffer");
	SWIM_REQUIRE_EQUAL(draws.size(), std::size_t(7 * 2 * 2));
	SWIM_REQUIRE_EQUAL(viewports.size(), std::size_t(7 * 2));
	SWIM_REQUIRE_EQUAL(scissors.size(), std::size_t(7 * 2));
	SWIM_REQUIRE_EQUAL(indexBuffers.size(), draws.size());
	std::vector<std::array<std::uint32_t, 4>> depthConstants;
	for (const auto& push : world.Commands("PushConstants"))
	{
		if (push.Data.size() == ShadowDepthBindings::PushConstantBytes)
		{
			std::array<std::uint32_t, 4> constants{};
			std::memcpy(constants.data(), push.Data.data(), sizeof(constants));
			depthConstants.push_back(constants);
		}
	}
	SWIM_REQUIRE_EQUAL(depthConstants.size(), draws.size());
	for (std::uint32_t v = 0; v < 7; ++v)
	{
		const auto& tile = world.plan.Draws[v].Tile;
		for (std::uint32_t variant = 0; variant < 2; ++variant)
		{
			const auto& viewport = viewports[v * 2 + variant];
			SWIM_CHECK(viewport.SourceOffset == tile.X && viewport.DestinationOffset == tile.Y && viewport.Size == tile.Size);
			const auto& scissor = scissors[v * 2 + variant];
			SWIM_CHECK(scissor.SourceOffset == tile.X && scissor.DestinationOffset == tile.Y && scissor.Size == tile.Size);
			for (std::uint32_t slot = 0; slot < 2; ++slot)
			{
				const std::size_t i = (v * 2 + variant) * 2 + slot;
				const auto bin = bins.GetBin(variant, slot);
				SWIM_CHECK(draws[i].Source == draws[v * 4].Source); // The view's own commands...
				SWIM_CHECK(draws[i].Destination == draws[v * 4].Destination);
				SWIM_CHECK_EQUAL(draws[i].SourceOffset, std::uint64_t(bins.GetRange(bin).First) * 20);
				SWIM_CHECK_EQUAL(draws[i].DestinationOffset, std::uint64_t(bin) * 4);
				SWIM_CHECK_EQUAL(draws[i].Size, std::uint64_t(variant == 0 ? ShadowWorld::OpaqueCapacity : ShadowWorld::MaskedCapacity));
				SWIM_CHECK(indexBuffers[i].Source == world.pages[slot == 0 ? 1 : 3].get());
				SWIM_CHECK_EQUAL(depthConstants[i][0], v);
				SWIM_CHECK_EQUAL(depthConstants[i][1], 4u); // Material rows.
			}
		}
		if (v > 0)
		{
			SWIM_CHECK(draws[v * 4].Source != draws[(v - 1) * 4].Source); // ...distinct per view.
		}
	}
	// Pipelines alternate opaque, masked per view; only the masked variant binds bindless.
	const auto pipelines = world.Commands("BindGraphicsPipeline");
	SWIM_REQUIRE_EQUAL(pipelines.size(), std::size_t(14));
	SWIM_CHECK(pipelines[0].Source == &world.opaquePipeline && pipelines[1].Source == &world.maskedPipeline);
	std::uint32_t bindlessBinds = 0;
	for (const auto& bind : world.Commands("BindDescriptorTable"))
	{
		if (bind.SourceOffset == ShadowDepthBindings::BindlessSpace)
		{
			SWIM_CHECK(bind.Source == world.bindlessTable.get());
			++bindlessBinds;
		}
	}
	SWIM_CHECK_EQUAL(bindlessBinds, 7u * 2u);

	// The last table: view 6, masked, slot 1.
	const auto* table = world.fixture.device.LastDescriptorTable;
	SWIM_REQUIRE(table != nullptr);
	SWIM_CHECK(table->Element(ShadowDepthBindings::Vertices, 0) == world.pages[2].get());
	SWIM_CHECK(table->Element(ShadowDepthBindings::Instances, 0) == world.instances.get());
	SWIM_CHECK(table->Element(ShadowDepthBindings::Transforms, 0) == world.transforms.get());
	SWIM_CHECK(table->Element(ShadowDepthBindings::Materials, 0) == world.materials.get());
	SWIM_CHECK(table->Element(ShadowDepthBindings::DrawRecords, 0) != nullptr);
	SWIM_CHECK(table->Element(ShadowDepthBindings::Views, 0) != nullptr);
	SWIM_CHECK_EQUAL(table->ElementWrites, ShadowDepthBindings::Count);
}

SWIM_TEST("Render.ShadowRenderer", "EmptyPlansClearTheAtlasAndTheFallbackPathZeroFills")
{
	ShadowWorld world;
	const ShadowRenderer renderer(world.Desc(VisibilityDrawPath::ZeroFilledIndirect));
	world.Plan({});
	{
		RenderGraph graph;
		auto frame = world.Import(graph);
		const auto resources = renderer.Record(graph, frame);
		graph.Export(resources.Atlas, Rhi::ResourceState::ShaderRead);
		SWIM_CHECK(resources.Visibility.empty());
		SWIM_CHECK_EQUAL(resources.ViewCount, 0u);
		SWIM_CHECK_EQUAL(graph.GetDesc(resources.Views).Size, std::uint64_t(sizeof(GpuShadowView))); // One placeholder view.
		world.Execute(graph);
		SWIM_CHECK(world.Commands("Dispatch").empty());
		SWIM_CHECK(world.Commands("DrawIndexedIndirect").empty());
	}

	world.Plan({ Caster(LightType::Directional, 0) });
	RenderGraph graph;
	auto frame = world.Import(graph);
	frame.ZeroUnusedCommands = true;
	const auto resources = renderer.Record(graph, frame);
	graph.Export(resources.Atlas, Rhi::ResourceState::ShaderRead);
	world.Execute(graph);
	SWIM_CHECK(world.Commands("DrawIndexedIndirectCount").empty());
	const auto draws = world.Commands("DrawIndexedIndirect");
	SWIM_REQUIRE_EQUAL(draws.size(), std::size_t(3 * 2 * 2)); // Three cascades.
	SWIM_CHECK_EQUAL(draws[3].Size, std::uint64_t(ShadowWorld::MaskedCapacity));
	SWIM_CHECK_EQUAL(
		draws[3].SourceOffset, std::uint64_t(world.visibility->GetBins().GetRange(world.visibility->GetBins().GetBin(1, 1)).First) * 20);
}

SWIM_TEST("Render.ShadowRenderer", "RejectsIncompleteFramesAndMismatchedVisibility")
{
	ShadowWorld world;
	const ShadowRenderer renderer(world.Desc());
	world.Plan({ Caster(LightType::Spot, 0) });
	const auto attempt = [&](const auto& edit)
	{
		RenderGraph graph;
		auto frame = world.Import(graph);
		edit(frame);
		renderer.Record(graph, frame);
	};
	SWIM_CHECK_THROWS(attempt(
						  [](ShadowFrame& f)
						  {
							  f.Plan = nullptr;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](ShadowFrame& f)
						  {
							  f.Scene = nullptr;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](ShadowFrame& f)
						  {
							  f.Bindless = nullptr;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](ShadowFrame& f)
						  {
							  f.PageSlots.clear();
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](ShadowFrame& f)
						  {
							  f.PageSlots[0].IndexPage = 9;
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](ShadowFrame& f)
						  {
							  f.PageSlots[1] = { 2, 2 };
						  }),
		std::invalid_argument);
	SWIM_CHECK_THROWS(attempt(
						  [](ShadowFrame& f)
						  {
							  f.PageSlots.push_back({ 1, 0 });
						  }),
		std::invalid_argument); // 3 slots vs 2.
	const auto twoBins = world.MakeVisibility({ 8, 4 });
	SWIM_CHECK_THROWS(attempt(
						  [&](ShadowFrame& f)
						  {
							  f.Visibility = twoBins.get();
						  }),
		std::invalid_argument);
	ShadowPlan empty;
	SWIM_CHECK_THROWS(attempt(
						  [&](ShadowFrame& f)
						  {
							  f.Plan = &empty;
						  }),
		std::invalid_argument);
}
