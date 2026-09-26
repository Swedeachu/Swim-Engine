#include "Engine/Systems/Renderer/ClusteredLighting/ClusteredLightAssigner.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Fixtures/GpuSceneFixture.h"

#include <cstring>

using namespace Swim;
using namespace Swim::Render;
namespace Scene = Swim::Testing::ClusterScene;

namespace
{
	struct AssignerWorld
	{
		AssignerWorld() : lights(fixture.device, { 2, 64, "Test lights" })
		{
			fixture.device.CreateTextures = true;
			using T = Rhi::DescriptorType;
			const auto schema = [](Testing::MockPipelineLayout& layout, std::initializer_list<std::pair<std::uint32_t, T>> bindings)
			{
				Rhi::DescriptorSchemaDesc space{ 0, {} };
				for (const auto& [binding, type] : bindings)
				{
					space.Bindings.push_back({ binding, type, 1, Rhi::ShaderStageMask::Compute });
				}
				layout.program.Interface.DescriptorSchemas = { space };
			};
			schema(cullLayout,
				{ { 0, T::ReadOnlyStorageBuffer }, { 1, T::ReadOnlyStorageBuffer }, { 2, T::ReadOnlyStorageBuffer },
					{ 3, T::StorageBuffer } });
			schema(boundsLayout, { { 0, T::ReadOnlyStorageBuffer }, { 1, T::StorageBuffer } });
			schema(assignLayout,
				{ { 0, T::ReadOnlyStorageBuffer }, { 1, T::ReadOnlyStorageBuffer }, { 2, T::ReadOnlyStorageBuffer },
					{ 3, T::ReadOnlyStorageBuffer }, { 4, T::StorageBuffer }, { 5, T::StorageBuffer } });
			schema(scanLayout,
				{ { 0, T::ReadOnlyStorageBuffer }, { 1, T::ReadOnlyStorageBuffer }, { 2, T::ReadOnlyStorageBuffer },
					{ 3, T::StorageBuffer }, { 4, T::StorageBuffer }, { 5, T::StorageBuffer } });
			schema(heatmapLayout,
				{ { 0, T::ReadOnlyStorageBuffer }, { 1, T::ReadOnlyStorageBuffer }, { 2, T::SampledTexture }, { 3, T::StorageTexture } });
		}

		ClusteredLightAssignerDesc Desc(bool heatmap = true)
		{
			ClusteredLightAssignerDesc desc;
			desc.Cull = { &cullPipeline, &cullLayout, 0 };
			desc.Bounds = { &boundsPipeline, &boundsLayout, 0 };
			desc.Assign = { &assignPipeline, &assignLayout, 0 };
			desc.Scan = { &scanPipeline, &scanLayout, 0 };
			if (heatmap)
			{
				desc.Heatmap = { &heatmapPipeline, &heatmapLayout, 0 };
			}
			return desc;
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
		GpuLightBuffer lights;
		Testing::MockPipelineLayout cullLayout, boundsLayout, assignLayout, scanLayout, heatmapLayout;
		Testing::MockComputePipeline cullPipeline, boundsPipeline, assignPipeline, scanPipeline, heatmapPipeline;
	};

	ClusterGridDesc GridDesc()
	{
		ClusterGridDesc desc;
		desc.ViewportWidth = 300;
		desc.ViewportHeight = 200;
		desc.TileSize = 32;
		desc.SliceCount = 12;
		desc.Far = 50.0f;
		desc.MaxLightsPerCluster = 16;
		desc.LightCapacity = 64; // Two mask words and one occupancy word per cluster.
		return desc;
	}
} // namespace

SWIM_TEST("Render.ClusteredLightAssigner", "RecordsCullBoundsMaskAndSummaryPasses")
{
	AssignerWorld world;
	SWIM_CHECK_THROWS(ClusteredLightAssigner(ClusteredLightAssignerDesc{}), std::invalid_argument);
	auto halfHeatmap = world.Desc(false);
	halfHeatmap.Heatmap.Pipeline = &world.heatmapPipeline;
	SWIM_CHECK_THROWS(ClusteredLightAssigner(halfHeatmap), std::invalid_argument);
	const ClusteredLightAssigner assigner(world.Desc());
	SWIM_CHECK(assigner.HasHeatmap());
	SWIM_CHECK(!ClusteredLightAssigner(world.Desc(false)).HasHeatmap());

	const auto scene = Scene::RandomScene(1, 40, 70);
	for (const auto& desc : scene.Descs)
	{
		world.lights.Create(desc);
	}
	RenderGraph graph;
	const auto lights = world.lights.Import(graph);
	const auto view = Scene::Camera(1.5f);
	const auto clusters = assigner.Record(graph, lights, GridDesc(), view);
	SWIM_CHECK_EQUAL(clusters.Passes.size(), std::size_t(4));
	SWIM_CHECK_EQUAL(clusters.Layout.ClusterCount, 10u * 7u * 12u);
	SWIM_CHECK_EQUAL(clusters.LocalLightCapacity, 64u);
	SWIM_CHECK_EQUAL(graph.GetDesc(clusters.Records).Size, std::uint64_t(840) * 16);
	SWIM_CHECK_EQUAL(graph.GetDesc(clusters.Indices).Size, std::uint64_t(840) * 3 * 4);
	SWIM_CHECK_EQUAL(graph.GetDesc(clusters.ViewLights).Size, std::uint64_t(64) * 16);
	SWIM_CHECK_EQUAL(graph.GetDesc(clusters.Bounds).Size, std::uint64_t(840) * 32);
	SWIM_CHECK_EQUAL(graph.GetDesc(clusters.Stats).Size, std::uint64_t(sizeof(ClusterStats)));
	graph.Export(clusters.Records, Rhi::ResourceState::ShaderRead);
	graph.Export(clusters.Indices, Rhi::ResourceState::ShaderRead);
	graph.Export(clusters.Stats, Rhi::ResourceState::ShaderRead);
	world.fixture.device.Commands->clear();
	world.fixture.executor->Execute(graph.Compile());
	world.fixture.executor->Wait();
	world.lights.CommitUploads();

	// Cull: one thread per local light; bounds: one per cluster; masks: one per (cluster,
	// mask word); summary: one per cluster.
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(4));
	SWIM_CHECK_EQUAL(dispatches[0].SourceOffset, 1u);  // 40 lights / 64.
	SWIM_CHECK_EQUAL(dispatches[1].SourceOffset, 14u); // 840 clusters / 64.
	SWIM_CHECK_EQUAL(dispatches[2].SourceOffset, 27u); // 1680 mask words / 64.
	SWIM_CHECK_EQUAL(dispatches[3].SourceOffset, 14u);
	const auto constants = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(constants.size(), std::size_t(1));
	std::array<std::uint32_t, 4> rowThreads{};
	std::memcpy(rowThreads.data(), constants[0].Data.data(), sizeof(rowThreads));
	SWIM_CHECK_EQUAL(rowThreads[0], 27u * 64u);

	// The grid record the passes read is MakeClusterGridRecord's.
	const auto expected = MakeClusterGridRecord(GridDesc(), view);
	SWIM_CHECK(std::memcmp(&clusters.GridRecord, &expected, sizeof(expected)) == 0);

	// Invalid inputs.
	RenderGraph bad;
	const auto badLights = world.lights.Import(bad);
	auto badGrid = GridDesc();
	badGrid.SliceCount = 0;
	SWIM_CHECK_THROWS(assigner.Record(bad, badLights, badGrid, view), std::invalid_argument);
	auto smallGrid = GridDesc();
	smallGrid.LightCapacity = 32; // 40 local lights do not fit.
	SWIM_CHECK_THROWS(assigner.Record(bad, badLights, smallGrid, view), std::invalid_argument);
	world.lights.AbortUploads();
}

SWIM_TEST("Render.ClusteredLightAssigner", "HeatmapNeedsAViewportSizedSampledDepth")
{
	AssignerWorld world;
	const ClusteredLightAssigner assigner(world.Desc());
	RenderGraph graph;
	const auto lights = world.lights.Import(graph);
	const auto clusters = assigner.Record(graph, lights, GridDesc(), Scene::Camera(1.5f));
	Rhi::TextureDesc depthDesc;
	depthDesc.Extent = { 300, 200, 1 };
	depthDesc.PixelFormat = Rhi::Format::R32Float;
	depthDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
	const auto depth = graph.CreateTexture(depthDesc);
	std::vector<float> texels(300 * 200, 0.05f);
	AddTextureUpload(graph, "Depth", std::as_bytes(std::span(texels)), depth, { 0, {}, {}, { 300, 200, 1 } });
	const auto heatmap = assigner.RecordHeatmap(graph, clusters, depth);
	const auto heatmapDesc = graph.GetDesc(heatmap);
	SWIM_CHECK(heatmapDesc.PixelFormat == Rhi::Format::RGBA8Unorm && heatmapDesc.Extent.Width == 300 && heatmapDesc.Extent.Height == 200);
	graph.Export(heatmap, Rhi::ResourceState::ShaderRead);
	world.fixture.device.Commands->clear();
	world.fixture.executor->Execute(graph.Compile());
	world.fixture.executor->Wait();
	world.lights.CommitUploads();
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE(!dispatches.empty());
	SWIM_CHECK(dispatches.back().SourceOffset == 38 && dispatches.back().DestinationOffset == 25); // 8x8 groups over 300x200.

	RenderGraph bad;
	const auto badLights = world.lights.Import(bad);
	const auto badClusters = assigner.Record(bad, badLights, GridDesc(), Scene::Camera(1.5f));
	auto wrongSize = depthDesc;
	wrongSize.Extent = { 320, 200, 1 };
	SWIM_CHECK_THROWS(assigner.RecordHeatmap(bad, badClusters, bad.CreateTexture(wrongSize)), std::invalid_argument);
	auto wrongFormat = depthDesc;
	wrongFormat.PixelFormat = Rhi::Format::RGBA8Unorm;
	SWIM_CHECK_THROWS(assigner.RecordHeatmap(bad, badClusters, bad.CreateTexture(wrongFormat)), std::invalid_argument);
	auto unsampled = depthDesc;
	unsampled.Usage = Rhi::TextureUsage::TransferDestination;
	SWIM_CHECK_THROWS(assigner.RecordHeatmap(bad, badClusters, bad.CreateTexture(unsampled)), std::invalid_argument);
	const ClusteredLightAssigner noHeatmap(world.Desc(false));
	SWIM_CHECK_THROWS(noHeatmap.RecordHeatmap(bad, badClusters, bad.CreateTexture(depthDesc)), std::logic_error);
	world.lights.AbortUploads();
}
