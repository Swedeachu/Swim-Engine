#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/Visibility/GpuDrawRecord.h"
#include "Engine/Systems/Renderer/Visibility/GpuLodState.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityStats.h"
#include "Tests/Fixtures/GpuSceneFixture.h"

#include <cstring>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	bool HasIndirect(const Rhi::Buffer& buffer)
	{
		return (static_cast<std::uint32_t>(buffer.GetDesc().Usage) & static_cast<std::uint32_t>(Rhi::BufferUsage::Indirect)) != 0;
	}

	// The culling program's reflected interface, as GpuVisibility.slang declares it.
	void DeclareCullingInterface(Testing::MockPipelineLayout& layout)
	{
		using B = GpuVisibilityBindings;
		Rhi::DescriptorSchemaDesc schema{ 0, {} };
		for (std::uint32_t binding = B::Instances; binding <= B::Stats; ++binding)
		{
			const bool writable = binding >= B::LodState;
			schema.Bindings.push_back({ binding, writable ? Rhi::DescriptorType::StorageBuffer : Rhi::DescriptorType::ReadOnlyStorageBuffer,
				1, Rhi::ShaderStageMask::Compute });
		}
		layout.program.Interface.DescriptorSchemas = { schema };
	}

	struct VisibilityWorld
	{
		VisibilityWorld() : scene(64)
		{
			DeclareCullingInterface(layout);
			GeometryHeapDesc heapDesc;
			heapDesc.VertexPageSize = 4096;
			heapDesc.IndexPageSize = 4096;
			heapDesc.MeshletPageSize = 1024;
			heapDesc.MaxMeshes = 8;
			heapDesc.MaxSubmeshes = 16;
			heapDesc.MaxPages = 8;
			geometry = std::make_unique<GeometryHeap>(scene.device, heapDesc);
		}

		GpuVisibility Make(std::uint32_t maxObjects = 64)
		{
			GpuVisibilityDesc desc;
			desc.CullPipeline = &pipeline;
			desc.Layout = &layout;
			desc.MaxObjects = maxObjects;
			desc.MaxMaterialSets = 8;
			desc.MaterialBinCapacities = { 32, 4 };
			desc.IndexPageSlots = 2;
			desc.DebugName = "Test visibility";
			return GpuVisibility(scene.device, std::move(desc));
		}

		// One frame: GPU Scene + geometry imports, visibility, execute.
		std::size_t Frame(GpuVisibility& visibility, const VisibilityFrameDesc& frame, VisibilityGraphResources* out = nullptr)
		{
			const auto copiesBefore = scene.CopyCount();
			RenderGraph graph;
			const auto sceneResources = scene.scene->Import(graph);
			const auto geometryResources = geometry->Import(graph);
			const auto resources = visibility.Record(graph, sceneResources, geometryResources, frame);
			const auto plan = graph.Compile();
			passes = plan.GetSchedule().size();
			const auto completion = scene.executor->Execute(plan);
			scene.scene->CommitUploads();
			geometry->CommitUploads(completion);
			scene.executor->Wait();
			if (out)
			{
				*out = resources;
			}
			return scene.CopyCount() - copiesBefore;
		}

		Testing::GpuSceneFixture scene;
		Testing::MockPipelineLayout layout;
		Testing::MockComputePipeline pipeline;
		std::unique_ptr<GeometryHeap> geometry;
		std::size_t passes = 0;
	};
} // namespace

SWIM_TEST("Render.GpuVisibility", "RecordsClearCullAndReadbackWithPersistentStateUploadedOnlyWhenNeeded")
{
	VisibilityWorld world;
	auto visibility = world.Make();
	SWIM_CHECK_EQUAL(visibility.GetBins().GetBinCount(), 4u);
	SWIM_CHECK_EQUAL(visibility.GetBins().GetTotalCapacity(), 72u);
	RenderObjectDesc object;
	object.MaterialSet = 1;
	world.scene.scene->Create(object);

	VisibilityFrameDesc frame;
	frame.View = BuildGpuViewRecord({});
	VisibilityGraphResources resources;
	// Frame 1: scene rows (2) + clear (2) + material table (1) + LOD history reset (1) + stats readback (1).
	SWIM_CHECK_EQUAL(world.Frame(visibility, frame, &resources), 7u);
	SWIM_REQUIRE(resources.StatsReadback.has_value());
	SWIM_CHECK(resources.Bins == &visibility.GetBins());
	SWIM_CHECK_EQUAL(world.passes, 7u); // Two scene uploads, clear, two persistent uploads, cull, readback.

	// The cull pass bound all thirteen descriptors, including the persistent buffers.
	auto* table = world.scene.device.LastDescriptorTable;
	SWIM_REQUIRE(table != nullptr);
	SWIM_CHECK_EQUAL(table->Elements.size(), 13u);
	SWIM_CHECK(table->Element(GpuVisibilityBindings::Instances, 0) == &world.scene.scene->GetInstanceBuffer());
	SWIM_CHECK(table->Element(GpuVisibilityBindings::Meshes, 0) == &world.geometry->GetMetadataBuffer());
	const auto* lodBuffer = static_cast<const Testing::MockMappedBuffer*>(table->Element(GpuVisibilityBindings::LodState, 0));
	SWIM_REQUIRE(lodBuffer != nullptr);
	SWIM_CHECK_EQUAL(lodBuffer->Bytes.size(), 64u * sizeof(GpuLodState));
	const auto* commands = static_cast<const Testing::MockMappedBuffer*>(table->Element(GpuVisibilityBindings::Commands, 0));
	SWIM_CHECK(commands->GetDesc().Size >= 72u * sizeof(Rhi::DrawIndexedIndirectCommand));
	SWIM_CHECK(HasIndirect(*commands));
	const auto* counts = static_cast<const Testing::MockMappedBuffer*>(table->Element(GpuVisibilityBindings::Counts, 0));
	SWIM_CHECK(HasIndirect(*counts));

	// Frame 2: nothing changed. Clear (2) + readback (1); the persistent tables stay.
	SWIM_CHECK_EQUAL(world.Frame(visibility, frame), 3u);
	// Material routing is uploaded again only after it changes.
	visibility.SetMaterialBin(1, 1);
	visibility.SetMaterialBin(2, 0); // Unchanged value.
	SWIM_CHECK_EQUAL(visibility.GetMaterialBin(1), 1u);
	SWIM_CHECK_EQUAL(visibility.GetMaterialBin(99), 0u);
	frame.ReadStats = false;
	SWIM_CHECK_EQUAL(world.Frame(visibility, frame, &resources), 3u); // Clear (2) + material table (1).
	SWIM_CHECK(!resources.StatsReadback.has_value());
	const auto* materialBins = static_cast<const Testing::MockMappedBuffer*>(
		world.scene.device.LastDescriptorTable->Element(GpuVisibilityBindings::MaterialBins, 0));
	std::uint32_t routed = 0;
	std::memcpy(&routed, materialBins->Bytes.data() + sizeof(std::uint32_t), sizeof(routed));
	SWIM_CHECK_EQUAL(routed, 1u);
}

SWIM_TEST("Render.GpuVisibility", "RejectsInvalidConfigurationsAndFrames")
{
	VisibilityWorld world;
	GpuVisibilityDesc desc;
	SWIM_CHECK_THROWS(GpuVisibility(world.scene.device, desc), std::invalid_argument); // No pipeline.
	desc.CullPipeline = &world.pipeline;
	desc.Layout = &world.layout;
	desc.MaterialBinCapacities = {};
	SWIM_CHECK_THROWS(GpuVisibility(world.scene.device, desc), std::invalid_argument);

	auto visibility = world.Make(2);
	SWIM_CHECK_THROWS(visibility.SetMaterialBin(8, 0), std::out_of_range);
	SWIM_CHECK_THROWS(visibility.SetMaterialBin(0, 2), std::out_of_range);
	for (int i = 0; i < 3; ++i)
	{
		world.scene.scene->Create({});
	}
	VisibilityFrameDesc frame;
	RenderGraph graph;
	const auto sceneResources = world.scene.scene->Import(graph);
	const auto geometryResources = world.geometry->Import(graph);
	SWIM_CHECK_THROWS(visibility.Record(graph, sceneResources, geometryResources, frame), std::length_error); // 3 rows > 2.
	frame.IndexPages = { 0, 1, 2 };
	auto roomy = world.Make();
	SWIM_CHECK_THROWS(roomy.Record(graph, sceneResources, geometryResources, frame), std::invalid_argument); // 3 pages > 2 slots.
	world.scene.scene->AbortUploads();
}

SWIM_TEST("Render.GpuVisibility", "RecordLayoutsMatchTheShaderContract")
{
	static_assert(sizeof(GpuViewRecord) == 192);
	static_assert(sizeof(VisibilityStats) == 16 * sizeof(std::uint32_t));
	static_assert(sizeof(GpuDrawRecord) == 8 && sizeof(GpuLodState) == 8 && sizeof(VisibilityBinRange) == 8);
	static_assert(sizeof(Rhi::DrawIndexedIndirectCommand) == 20);
	RenderViewDesc desc;
	desc.CameraPosition = { 1, 2, 3 };
	desc.LodScale = 700.0f;
	desc.Flags = std::uint32_t(GpuViewFlags::ResetLodHistory);
	const auto view = BuildGpuViewRecord(desc);
	SWIM_CHECK_EQUAL(view.CameraPosition[2], 3.0f);
	SWIM_CHECK_EQUAL(view.LodScale, 700.0f);
	SWIM_CHECK_EQUAL(view.Flags, 2u);
	SWIM_CHECK_EQUAL(view.ViewProjection[15], 1.0f);
}
