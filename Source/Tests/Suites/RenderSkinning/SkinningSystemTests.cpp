#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/Skinning/SkinningSystem.h"
#include "Tests/Fixtures/RhiFrameCapture.h"
#include "Tests/Fixtures/SkinningFixture.h"
#include "Tests/Framework/Test.h"

#include <cstring>
#include <memory>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	// A skinning system over a small GeometryHeap on the mock device: host-backed
	// buffers receive the graph's uploads; dispatches and push constants are recorded.
	struct SkinningWorld
	{
		explicit SkinningWorld(std::uint64_t vertexPageSize = 64 * 1024)
		{
			executor = std::make_unique<RenderGraphExecutor>(device);
			using T = Rhi::DescriptorType;
			using B = SkinningBindings;
			Rhi::DescriptorSchemaDesc space{ 0, {} };
			for (const auto& [binding, type] : { std::pair{ B::Dispatches, T::ReadOnlyStorageBuffer },
					 std::pair{ B::SourceVertices, T::ReadOnlyStorageBuffer }, std::pair{ B::SkinVertices, T::ReadOnlyStorageBuffer },
					 std::pair{ B::MorphDeltas, T::ReadOnlyStorageBuffer }, std::pair{ B::Palettes, T::ReadOnlyStorageBuffer },
					 std::pair{ B::MorphWeights, T::ReadOnlyStorageBuffer }, std::pair{ B::Output, T::StorageBuffer } })
			{
				space.Bindings.push_back({ binding, type, 1, Rhi::ShaderStageMask::Compute });
			}
			layout.program.Interface.DescriptorSchemas = { space };
			GeometryHeapDesc heapDesc;
			heapDesc.VertexPageSize = vertexPageSize;
			heapDesc.IndexPageSize = 64 * 1024;
			heapDesc.MeshletPageSize = 1024;
			heapDesc.MaxMeshes = 32;
			heapDesc.MaxPages = 16;
			heap = std::make_unique<GeometryHeap>(device, heapDesc);
		}

		SkinningSystemDesc Desc(std::uint32_t vertices = 4096, std::uint32_t deltas = 4096)
		{
			SkinningSystemDesc desc;
			desc.Pipeline = &pipeline;
			desc.Layout = &layout;
			desc.MaxSourceVertices = vertices;
			desc.MaxMorphDeltas = deltas;
			desc.MaxMeshes = 4;
			desc.MaxInstances = 8;
			desc.DebugName = "Test skinning";
			return desc;
		}

		SkinnedMeshDesc MeshDesc(const Testing::SkinnedStrip& strip) const
		{
			SkinnedMeshDesc desc;
			desc.Vertices = strip.Vertices;
			desc.Influences = strip.Influences;
			desc.MorphTargets = strip.Targets;
			desc.JointCount = Testing::SkinnedStrip::JointCount;
			desc.Indices = strip.IndexBytes();
			desc.DebugName = "Strip";
			return desc;
		}

		// One frame: heap import, skinning, execute, commit.
		SkinningGraphResources Frame(SkinningSystem& system)
		{
			device.Commands->clear();
			RenderGraph graph;
			const auto geometry = heap->Import(graph);
			auto resources = system.Record(graph, geometry);
			const auto completion = executor->Execute(graph.Compile());
			executor->Wait();
			heap->CommitUploads(completion);
			system.CommitFrame();
			heap->Collect();
			return resources;
		}

		std::vector<Testing::MockCommand> Commands(const std::string& kind) const
		{
			std::vector<Testing::MockCommand> result;
			for (const auto& command : *device.Commands)
			{
				if (command.Kind == kind)
				{
					result.push_back(command);
				}
			}
			return result;
		}

		Testing::MockDevice device;
		std::unique_ptr<RenderGraphExecutor> executor;
		Testing::MockPipelineLayout layout;
		Testing::MockComputePipeline pipeline;
		std::unique_ptr<GeometryHeap> heap;
	};

	SkinPose Pose(const std::vector<SkinMatrix>& palette, const std::vector<SkinMatrix>& previous, const std::vector<float>& weights,
		const std::vector<float>& previousWeights)
	{
		return { palette, previous, weights, previousWeights };
	}

	const std::vector<SkinMatrix> Identity(3, SkinMatrix{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 });
	const std::vector<float> NoMorph{ 0.0f, 0.0f };
} // namespace

SWIM_TEST("Render.SkinningSystem", "OutputMeshesUploadSourcesAndSkinDirtyThenSettlingInstances")
{
	SkinningWorld world;
	SkinningSystem system(world.device, *world.heap, world.Desc());
	const Testing::SkinnedStrip strip;
	const auto mesh = system.CreateSkinnedMesh(world.MeshDesc(strip));
	const auto instance = system.CreateInstance(mesh);
	const auto vertexCount = static_cast<std::uint32_t>(strip.Vertices.size());
	SWIM_CHECK_EQUAL(system.GetPreviousVertexOffset(instance), vertexCount);
	const GpuMeshMetadata* output = world.heap->GetMetadata(system.GetOutputMesh(instance));
	SWIM_REQUIRE(output != nullptr);
	SWIM_CHECK_EQUAL(output->VertexCount, vertexCount * 2);
	SWIM_CHECK_EQUAL(output->VertexLayout, StandardVertexLayoutId());
	SWIM_CHECK_EQUAL(output->IndexCount, std::uint32_t(strip.Indices.size()));
	SWIM_CHECK(system.ComputeBounds(instance) == RenderBounds::Infinite()); // No pose yet.

	// Without a pose only the source is uploaded.
	auto first = world.Frame(system);
	SWIM_CHECK_EQUAL(first.UploadedMeshes, 1u);
	SWIM_CHECK(first.Instances.empty() && !first.SkinPass);
	SWIM_CHECK(world.Commands("Dispatch").empty());
	auto& host = static_cast<Testing::MockMappedBuffer&>(system.GetSourceVertexBuffer());
	SWIM_CHECK(std::memcmp(host.Bytes.data(), strip.Vertices.data(), strip.Vertices.size() * sizeof(StandardVertex)) == 0);
	SWIM_CHECK_EQUAL(system.GetStats().PendingMeshes, 0u);

	std::vector<SkinMatrix> moved = Identity;
	moved[2][3] = 1.0f;
	const std::vector<float> weights{ 0.5f, 0.0f };
	SWIM_CHECK(system.SetPose(instance, Pose(moved, Identity, weights, NoMorph)));
	auto second = world.Frame(system);
	SWIM_CHECK_EQUAL(second.UploadedMeshes, 0u);
	SWIM_REQUIRE_EQUAL(second.Instances.size(), std::size_t(1));
	const GpuSkinDispatch& row = second.Instances[0].Record;
	SWIM_CHECK(!second.Instances[0].Settling);
	SWIM_CHECK_EQUAL(row.VertexCount, vertexCount);
	SWIM_CHECK_EQUAL(row.OutputVertex, output->VertexOffset);
	SWIM_CHECK_EQUAL(row.PreviousOffset, vertexCount);
	SWIM_CHECK_EQUAL(row.JointCount, 3u);
	SWIM_CHECK_EQUAL(row.MorphTargetCount, 2u);
	SWIM_CHECK_EQUAL(second.Instances[0].Page, output->VertexPage);
	const auto dispatches = world.Commands("Dispatch");
	SWIM_REQUIRE_EQUAL(dispatches.size(), std::size_t(1));
	SWIM_CHECK(dispatches[0].SourceOffset == (vertexCount + 63) / 64 && dispatches[0].DestinationOffset == 1u);
	const auto constants = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(constants.size(), std::size_t(1));
	std::uint32_t firstRow = 99;
	std::memcpy(&firstRow, constants[0].Data.data(), 4);
	SWIM_CHECK_EQUAL(firstRow, 0u);
	SWIM_CHECK(world.Commands("BindComputePipeline")[0].Source == &world.pipeline);
	// Bounds follow the current pose: the tip moved +1 in x.
	SWIM_CHECK(system.ComputeBounds(instance).Center[0] > 0.2f);

	// No new pose: one settling pass with previous = current, then nothing.
	auto third = world.Frame(system);
	SWIM_REQUIRE_EQUAL(third.Instances.size(), std::size_t(1));
	SWIM_CHECK(third.Instances[0].Settling);
	auto fourth = world.Frame(system);
	SWIM_CHECK(fourth.Instances.empty() && !fourth.SkinPass);
	SWIM_CHECK(world.Commands("Dispatch").empty());
}

SWIM_TEST("Render.SkinningSystem", "InstancesWaitForTheirOutputMeshAndAbortReplaysTheFrame")
{
	SkinningWorld world;
	SkinningSystem system(world.device, *world.heap, world.Desc());
	const Testing::SkinnedStrip strip;
	const auto mesh = system.CreateSkinnedMesh(world.MeshDesc(strip));

	// Created after the heap import of this graph: its output mesh is not in it yet.
	RenderGraph graph;
	const auto geometry = world.heap->Import(graph);
	const auto late = system.CreateInstance(mesh);
	system.SetPose(late, Pose(Identity, Identity, NoMorph, NoMorph));
	const auto skipped = system.Record(graph, geometry);
	SWIM_CHECK_EQUAL(skipped.SkippedInstances, 1u);
	SWIM_CHECK(skipped.Instances.empty());
	SWIM_CHECK_THROWS(system.Record(graph, geometry), std::logic_error); // Awaiting commit/abort.
	system.AbortFrame();
	world.heap->AbortUploads();

	// Abort put the source back to pending: the next frame uploads it again and skins.
	const auto replay = world.Frame(system);
	SWIM_CHECK_EQUAL(replay.UploadedMeshes, 1u);
	SWIM_CHECK_EQUAL(replay.Instances.size(), std::size_t(1));

	// An aborted skin pass is recorded again (not as settling).
	system.SetPose(late, Pose(Identity, Identity, NoMorph, NoMorph));
	RenderGraph aborted;
	const auto abortedGeometry = world.heap->Import(aborted);
	SWIM_CHECK_EQUAL(system.Record(aborted, abortedGeometry).Instances.size(), std::size_t(1));
	system.AbortFrame();
	world.heap->AbortUploads();
	const auto again = world.Frame(system);
	SWIM_REQUIRE_EQUAL(again.Instances.size(), std::size_t(1));
	SWIM_CHECK(!again.Instances[0].Settling);
}

SWIM_TEST("Render.SkinningSystem", "OnePassDispatchesPerOutputPage")
{
	// Each instance needs 2 x 72 x 48 bytes: one per 8 KiB vertex page.
	SkinningWorld world(8 * 1024);
	SkinningSystem system(world.device, *world.heap, world.Desc());
	const Testing::SkinnedStrip strip;
	const auto mesh = system.CreateSkinnedMesh(world.MeshDesc(strip));
	std::vector<SkinInstanceHandle> handles;
	for (int i = 0; i < 3; ++i)
	{
		handles.push_back(system.CreateInstance(mesh));
		system.SetPose(handles.back(), Pose(Identity, Identity, NoMorph, NoMorph));
	}
	const auto frame = world.Frame(system);
	SWIM_REQUIRE_EQUAL(frame.Instances.size(), std::size_t(3));
	SWIM_CHECK(frame.Instances[0].Page < frame.Instances[1].Page && frame.Instances[1].Page < frame.Instances[2].Page);
	SWIM_CHECK_EQUAL(world.Commands("Dispatch").size(), std::size_t(3));
	const auto constants = world.Commands("PushConstants");
	SWIM_REQUIRE_EQUAL(constants.size(), std::size_t(3));
	for (std::uint32_t i = 0; i < 3; ++i)
	{
		std::uint32_t firstRow = 99;
		std::memcpy(&firstRow, constants[i].Data.data(), 4);
		SWIM_CHECK_EQUAL(firstRow, i);
		SWIM_CHECK_EQUAL(frame.Instances[i].Record.Palette, i * 6); // Three current + three previous matrices each.
		SWIM_CHECK_EQUAL(frame.Instances[i].Record.MorphWeights, i * 4);
	}
}

SWIM_TEST("Render.SkinningSystem", "LifetimesCapacitiesAndPoseValidation")
{
	SkinningWorld world;
	const Testing::SkinnedStrip strip;
	{
		SkinningSystem tiny(world.device, *world.heap, world.Desc(80, 4096));
		tiny.CreateSkinnedMesh(world.MeshDesc(strip));
		SWIM_CHECK_THROWS(tiny.CreateSkinnedMesh(world.MeshDesc(strip)), std::length_error); // 72 of 80 vertices used.
	}
	SkinningSystemDesc missing = world.Desc();
	missing.Pipeline = nullptr;
	SWIM_CHECK_THROWS(SkinningSystem(world.device, *world.heap, missing), std::invalid_argument);

	SkinningSystem system(world.device, *world.heap, world.Desc());
	const auto mesh = system.CreateSkinnedMesh(world.MeshDesc(strip));
	const auto instance = system.CreateInstance(mesh);
	SWIM_CHECK_THROWS(system.SetPose(instance, Pose({ Identity[0] }, {}, NoMorph, {})), std::invalid_argument);
	SWIM_CHECK_THROWS(system.SetPose(instance, Pose(Identity, {}, { 1.0f }, {})), std::invalid_argument);
	std::vector<SkinMatrix> broken = Identity;
	broken[1][0] = std::nanf("");
	SWIM_CHECK_THROWS(system.SetPose(instance, Pose(broken, {}, NoMorph, {})), std::invalid_argument);
	SWIM_CHECK(system.SetPose(instance, Pose(Identity, {}, NoMorph, {}))); // Empty previous = current.

	world.Frame(system);
	SWIM_CHECK(!system.DestroySkinnedMesh(mesh)); // An instance still uses it.
	const auto output = system.GetOutputMesh(instance);
	SWIM_CHECK(system.DestroyInstance(instance));
	SWIM_CHECK(!system.IsValid(instance) && !world.heap->IsValid(output));
	SWIM_CHECK(!system.SetPose(instance, Pose(Identity, {}, NoMorph, {})));
	SWIM_CHECK(system.DestroySkinnedMesh(mesh));
	SWIM_CHECK(!system.IsValid(mesh));
	SWIM_CHECK_EQUAL(system.Collect(), std::size_t(2));
	SWIM_CHECK_EQUAL(system.GetStats().SourceVertices, 0u);
	SWIM_CHECK_THROWS(system.CreateInstance(mesh), std::invalid_argument);
	const auto reused = system.CreateSkinnedMesh(world.MeshDesc(strip));
	SWIM_CHECK(system.IsValid(reused));
}
