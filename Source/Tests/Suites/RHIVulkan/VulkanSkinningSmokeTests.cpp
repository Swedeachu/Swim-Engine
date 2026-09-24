#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Animation/Animator.h"
#include "Engine/Systems/Animation/SkeletonInstance.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Skinning/SkinningSystem.h"
#include "Tests/Fixtures/SkinningFixture.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_SKINNING_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) &&    \
	defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH)
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_SKINNING_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_SKINNING_SMOKE_AVAILABLE
	namespace Smoke = Swim::Testing::EnvironmentSmoke;

	double PassMilliseconds(const std::vector<Swim::Render::GraphPassTiming>& timings, std::string_view prefix)
	{
		double total = 0.0;
		double previousEnd = 0.0;
		for (const auto& timing : timings)
		{
			if (!timing.EndOffsetNanoseconds)
			{
				continue;
			}
			const double end = *timing.EndOffsetNanoseconds;
			if (timing.Name.starts_with(prefix))
			{
				total += std::max(end - previousEnd, 0.0) * 1.0e-6;
			}
			previousEnd = std::max(previousEnd, end);
		}
		return total;
	}

	bool Close(float actual, float expected, float tolerance)
	{
		return std::abs(actual - expected) <= tolerance * (1.0f + std::abs(expected));
	}
#endif

	// GPU skinning (critical-path item 78): animated characters (Animator ->
	// SkeletonInstance -> SkinningSystem) skinned on the GPU into GeometryHeap output
	// meshes, read back every frame and compared vertex by vertex with
	// Skinning::SkinVertex (current) and Skinning::SkinPosition (previous), plus the
	// settling frame, the idle frames after it and the pose bounds. A final frame
	// times 64 characters.
	void RunSkinningSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_SKINNING_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Skinning smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Skinning smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const auto program = Smoke::MakeCompute(*device, SWIM_SKINNING_SPIRV_PATH, SWIM_SKINNING_REFLECTION_PATH, "Skinning");
		RenderGraphExecutor executor(*device);

		GeometryHeapDesc heapDesc;
		heapDesc.VertexPageSize = 8ull << 20;
		heapDesc.IndexPageSize = 1ull << 20;
		heapDesc.MeshletPageSize = 4096;
		heapDesc.MaxMeshes = 128;
		heapDesc.MaxPages = 16;
		GeometryHeap heap(*device, heapDesc);
		SkinningSystemDesc desc;
		desc.Pipeline = program.Pipeline.get();
		desc.Layout = program.Layout.get();
		desc.MaxSourceVertices = 1u << 16;
		desc.MaxMorphDeltas = 1u << 16;
		desc.MaxMeshes = 8;
		desc.MaxInstances = 128;
		desc.DebugName = "Skinning";
		SkinningSystem system(*device, heap, desc);

		// Two meshes (different pool offsets), three characters.
		const Testing::SkinnedStrip small(9, 0.25f);
		const Testing::SkinnedStrip large(33, 0.4f);
		const auto meshDesc = [](const Testing::SkinnedStrip& strip, const char* name)
		{
			SkinnedMeshDesc mesh;
			mesh.Vertices = strip.Vertices;
			mesh.Influences = strip.Influences;
			mesh.MorphTargets = strip.Targets;
			mesh.JointCount = Testing::SkinnedStrip::JointCount;
			mesh.Indices = strip.IndexBytes();
			mesh.DebugName = name;
			return mesh;
		};
		const auto smallMesh = system.CreateSkinnedMesh(meshDesc(small, "Small strip"));
		const auto largeMesh = system.CreateSkinnedMesh(meshDesc(large, "Large strip"));
		const SkinnedSource smallSource = Skinning::BuildSource(small.Vertices, small.Influences, small.Targets, 3);
		const SkinnedSource largeSource = Skinning::BuildSource(large.Vertices, large.Influences, large.Targets, 3);

		struct Character
		{
			const Testing::SkinnedStrip* Strip;
			const SkinnedSource* Source;
			SkinInstanceHandle Handle;
			std::unique_ptr<Animation::Animator> Animator;
			std::unique_ptr<Animation::SkeletonInstance> Skeleton;
			std::vector<float> Weights{ 0.0f, 0.0f };
			std::vector<float> PreviousWeights{ 0.0f, 0.0f };
			float Phase = 0.0f;
			int StopPosingAt = 1 << 30;
		};

		const auto skeleton = Testing::MakeChainSkeleton();
		const auto clip = Testing::MakeWaveClip();
		std::vector<Character> characters;
		const auto addCharacter = [&](const Testing::SkinnedStrip& strip, const SkinnedSource& source, SkinnedMeshHandle mesh, float speed,
									  float phase, int stopAt)
		{
			Animation::AnimatorDesc animatorDesc;
			animatorDesc.SharedSkeleton = skeleton;
			Animation::AnimatorLayerDesc layer;
			layer.States.push_back({ "Wave", clip, speed, true, phase });
			animatorDesc.Layers.push_back(layer);
			Character character;
			character.Strip = &strip;
			character.Source = &source;
			character.Handle = system.CreateInstance(mesh);
			character.Animator = std::make_unique<Animation::Animator>(animatorDesc);
			character.Skeleton = std::make_unique<Animation::SkeletonInstance>(skeleton);
			character.Phase = phase;
			character.StopPosingAt = stopAt;
			characters.push_back(std::move(character));
		};
		addCharacter(small, smallSource, smallMesh, 1.0f, 0.0f, 1 << 30);
		addCharacter(large, largeSource, largeMesh, 1.3f, 0.25f, 1 << 30);
		addCharacter(small, smallSource, smallMesh, -0.7f, 0.6f, 6); // Stops posing: settles, then idles.

		constexpr int Frames = 10;
		constexpr float Dt = 1.0f / 30.0f;
		std::uint64_t comparedVertices = 0, outliers = 0, settledChecked = 0, boundsViolations = 0;
		float worst = 0.0f, worstPrevious = 0.0f;
		for (int frame = 0; frame < Frames; ++frame)
		{
			const float time = float(frame) * Dt;
			for (Character& character : characters)
			{
				if (frame >= character.StopPosingAt)
				{
					continue;
				}
				character.Animator->Update(Dt);
				character.Skeleton->Update(character.Animator->GetPose());
				character.PreviousWeights = frame == 0 ? std::vector<float>{ 0.0f, 0.0f } : character.Weights;
				character.Weights = { 0.5f + 0.5f * std::sin(6.0f * time + character.Phase), frame % 3 == 0 ? 1.0f : 0.25f };
				if (frame == 0)
				{
					character.PreviousWeights = character.Weights;
				}
				SWIM_REQUIRE(system.SetPose(character.Handle,
					{ character.Skeleton->GetSkinningMatrices(), character.Skeleton->GetPreviousSkinningMatrices(), character.Weights,
						character.PreviousWeights }));
			}

			RenderGraph graph;
			const auto geometry = heap.Import(graph);
			const auto resources = system.Record(graph, geometry);
			std::vector<std::pair<const Character*, GraphReadback>> readbacks;
			for (const Character& character : characters)
			{
				const GpuMeshMetadata& output = *heap.GetMetadata(system.GetOutputMesh(character.Handle));
				readbacks.push_back({ &character,
					AddBufferReadback(graph, "Skinned output", geometry.Pages[output.VertexPage],
						std::uint64_t(output.VertexOffset) * StandardVertexStride,
						std::uint64_t(output.VertexCount) * StandardVertexStride) });
			}
			const auto completion = executor.Execute(graph.Compile());
			executor.Wait();
			heap.CommitUploads(completion);
			system.CommitFrame();
			heap.Collect();

			std::uint32_t skinned = 0, settling = 0;
			for (const auto& instance : resources.Instances)
			{
				skinned += instance.Settling ? 0u : 1u;
				settling += instance.Settling ? 1u : 0u;
			}
			// Every posing character skins; the one that stopped settles once, then idles.
			const std::uint32_t posing = frame < 6 ? 3u : 2u;
			SWIM_CHECK_EQUAL(skinned, posing);
			SWIM_CHECK_EQUAL(settling, frame == 6 ? 1u : 0u);

			for (const auto& [character, readback] : readbacks)
			{
				const auto vertexCount = character->Strip->Vertices.size();
				std::vector<StandardVertex> gpu(vertexCount * 2);
				SWIM_REQUIRE(executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(gpu))) == Rhi::ReadbackStatus::Ready);
				const bool stopped = frame >= character->StopPosingAt;
				const auto palette = character->Skeleton->GetSkinningMatrices();
				// A settled character's previous positions equal its current ones.
				const auto previousPalette = stopped ? palette : character->Skeleton->GetPreviousSkinningMatrices();
				const auto& previousWeights = stopped ? character->Weights : character->PreviousWeights;
				const RenderBounds bounds = system.ComputeBounds(character->Handle);
				for (std::size_t v = 0; v < vertexCount; ++v)
				{
					const GpuSkinVertex& skin = character->Source->SkinVertices[v];
					const auto deltas = std::span(character->Source->MorphDeltas).subspan(skin.MorphFirst, skin.MorphCount);
					const StandardVertex expected =
						Skinning::SkinVertex(character->Strip->Vertices[v], skin, deltas, palette, character->Weights);
					const auto previous =
						Skinning::SkinPosition(character->Strip->Vertices[v], skin, deltas, previousPalette, previousWeights);
					const StandardVertex& actual = gpu[v];
					bool bad = false;
					for (int c = 0; c < 3; ++c)
					{
						worst = std::max(worst, std::abs(actual.Position[c] - expected.Position[c]));
						worstPrevious = std::max(worstPrevious, std::abs(gpu[vertexCount + v].Position[c] - previous[c]));
						bad = bad || !Close(actual.Position[c], expected.Position[c], 1e-5f) ||
							!Close(gpu[vertexCount + v].Position[c], previous[c], 1e-5f) ||
							!Close(actual.Normal[c], expected.Normal[c], 1e-4f) || !Close(actual.Tangent[c], expected.Tangent[c], 1e-4f);
						const bool inside = std::abs(actual.Position[c] - bounds.Center[c]) <= bounds.Extents[c] + 1e-4f;
						boundsViolations += inside ? 0u : 1u;
					}
					bad = bad || actual.Tangent[3] != expected.Tangent[3] || actual.TexCoord0 != expected.TexCoord0;
					outliers += bad ? 1u : 0u;
					++comparedVertices;
					if (stopped)
					{
						++settledChecked;
					}
				}
			}
		}
		std::printf("             [skinning] %d frames, %llu vertices compared: %llu outliers (worst position %.2e, previous %.2e); "
					"%llu settled vertices; %llu outside the pose bounds\n",
			Frames, static_cast<unsigned long long>(comparedVertices), static_cast<unsigned long long>(outliers), double(worst),
			double(worstPrevious), static_cast<unsigned long long>(settledChecked), static_cast<unsigned long long>(boundsViolations));
		SWIM_CHECK_EQUAL(outliers, std::uint64_t(0));
		SWIM_CHECK_EQUAL(boundsViolations, std::uint64_t(0));
		SWIM_CHECK(settledChecked > 0u);

		// Timing: 64 characters of 1,032 vertices (129 rings), about 66k vertices.
		const Testing::SkinnedStrip dense(129, 0.3f);
		const auto denseMesh = system.CreateSkinnedMesh(meshDesc(dense, "Dense strip"));
		std::vector<SkinInstanceHandle> crowd;
		Animation::SkeletonInstance crowdSkeleton(skeleton);
		const std::vector<float> weights{ 0.3f, 0.6f };
		for (int i = 0; i < 64; ++i)
		{
			crowd.push_back(system.CreateInstance(denseMesh));
		}
		for (int frame = 0; frame < 3; ++frame)
		{
			for (const auto handle : crowd)
			{
				system.SetPose(
					handle, { crowdSkeleton.GetSkinningMatrices(), crowdSkeleton.GetPreviousSkinningMatrices(), weights, weights });
			}
			RenderGraph graph;
			const auto geometry = heap.Import(graph);
			const auto resources = system.Record(graph, geometry);
			const auto completion = executor.Execute(graph.Compile());
			executor.Wait();
			heap.CommitUploads(completion);
			system.CommitFrame();
			heap.Collect();
			if (frame == 2)
			{
				SWIM_CHECK_EQUAL(resources.Instances.size(), std::size_t(64));
				std::printf("             [skinning 64 x %zu vertices] GPU: skin %.3f ms\n", dense.Vertices.size(),
					PassMilliseconds(executor.ReadTimings(), "Skinning skin"));
			}
		}
		executor.Wait();
		for (const auto handle : crowd)
		{
			system.DestroyInstance(handle);
		}
		for (Character& character : characters)
		{
			system.DestroyInstance(character.Handle);
		}
		system.Drain();
		heap.Drain();
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "GpuSkinningMatchesTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunSkinningSmoke);
				} });
		}
		return true;
	}();
} // namespace
