#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterReference.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusteredLightAssigner.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_CLUSTER_LIGHT_CULL_SPIRV_PATH) && defined(SWIM_CLUSTER_BOUNDS_SPIRV_PATH) && defined(SWIM_CLUSTER_ASSIGN_SPIRV_PATH) &&   \
	defined(SWIM_CLUSTER_SCAN_SPIRV_PATH) && defined(SWIM_CLUSTER_HEATMAP_SPIRV_PATH) &&                                                   \
	defined(SWIM_RHI_CLUSTERED_LIGHT_PROBE_SPIRV_PATH)
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#define SWIM_CLUSTERED_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <span>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_CLUSTERED_SMOKE_AVAILABLE
	// Mirrors ClusteredLightProbe.slang's ClusterProbeSample (std430, 80 bytes).
	struct ProbeSample
	{
		float Position[3];
		float Metallic;
		float Normal[3];
		float Roughness;
		float View[3];
		float ViewDepth;
		float BaseColor[3];
		float Reserved;
		float Pixel[2];
		float Reserved1[2];
	};

	static_assert(sizeof(ProbeSample) == 80);

	struct ComputeProgram
	{
		std::unique_ptr<Swim::Rhi::ShaderProgram> Program;
		std::unique_ptr<Swim::Rhi::PipelineLayout> Layout;
		std::unique_ptr<Swim::Rhi::ComputePipeline> Pipeline;

		Swim::Render::ClusterProgram Get() const { return { Pipeline.get(), Layout.get(), 0 }; }
	};

	ComputeProgram LoadCompute(Swim::Rhi::Device& device, const char* spirvPath, const char* reflectionPath, const char* label)
	{
		using namespace Swim;
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(reflectionPath);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file && !bytes.empty());
		const auto& programInterface = converted.Interface;
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytes };
		ComputeProgram program;
		program.Program = device.CreateShaderProgram({ { &stage, 1 },
			{ programInterface.DescriptorSchemas, programInterface.PushConstants, programInterface.ComputeThreadGroupSize }, label });
		SWIM_REQUIRE(program.Program);
		program.Layout = device.CreatePipelineLayout({ program.Program.get(), label });
		SWIM_REQUIRE(program.Layout);
		program.Pipeline = device.CreateComputePipeline({ program.Program.get(), program.Layout.get(), {}, label });
		SWIM_REQUIRE(program.Pipeline);
		return program;
	}
#endif

	// Critical-path items 64, 65 and 68 on a real device. A GpuLightBuffer holds 2
	// directional and 1,500 point/spot lights around a camera; ClusteredLightAssigner
	// runs its five passes and every output is compared with Clustering::* over the
	// same inputs:
	//  - view-space light spheres and cluster AABBs against CullLight/ClusterBounds;
	//  - every cluster's record and light list against AssignLights over the GPU's
	//    own spheres and bounds (differences allowed only for spheres touching an AABB
	//    within float rounding);
	//  - ClusterStats;
	//  - a compute probe shading 1,024 points with ClusteredShade equals the
	//    brute-force sum (GPU) and Lights::ShadeAllLights (CPU);
	//  - the heatmap of a synthetic depth buffer against HeatmapPixel.
	// Frames: 1280x720 with room for every list; the same lights with 4 lights per
	// cluster and half the needed index capacity (truncation, capacity overflow,
	// magenta heatmap); 800x450 after 300 lights moved (a regenerated grid).
	void RunClusteredLightingSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_CLUSTERED_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Clustered lighting smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;
		namespace Cl = Swim::Render::Clustering;
		namespace Scene = Swim::Testing::ClusterScene;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Clustered lighting smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const auto cull = LoadCompute(*device, SWIM_CLUSTER_LIGHT_CULL_SPIRV_PATH, SWIM_CLUSTER_LIGHT_CULL_REFLECTION_PATH, "Cluster cull");
		const auto bounds = LoadCompute(*device, SWIM_CLUSTER_BOUNDS_SPIRV_PATH, SWIM_CLUSTER_BOUNDS_REFLECTION_PATH, "Cluster bounds");
		const auto assign = LoadCompute(*device, SWIM_CLUSTER_ASSIGN_SPIRV_PATH, SWIM_CLUSTER_ASSIGN_REFLECTION_PATH, "Cluster assign");
		const auto scan = LoadCompute(*device, SWIM_CLUSTER_SCAN_SPIRV_PATH, SWIM_CLUSTER_SCAN_REFLECTION_PATH, "Cluster scan");
		const auto heatmap = LoadCompute(*device, SWIM_CLUSTER_HEATMAP_SPIRV_PATH, SWIM_CLUSTER_HEATMAP_REFLECTION_PATH, "Cluster heatmap");
		const auto probe = LoadCompute(
			*device, SWIM_RHI_CLUSTERED_LIGHT_PROBE_SPIRV_PATH, SWIM_RHI_CLUSTERED_LIGHT_PROBE_REFLECTION_PATH, "Clustered light probe");
		ClusteredLightAssignerDesc assignerDesc;
		assignerDesc.Cull = cull.Get();
		assignerDesc.Bounds = bounds.Get();
		assignerDesc.Assign = assign.Get();
		assignerDesc.Scan = scan.Get();
		assignerDesc.Heatmap = heatmap.Get();
		const ClusteredLightAssigner assigner(assignerDesc);

		RenderGraphExecutor executor(*device);
		GpuLightBuffer lights(*device, { 4, 2048, "Clustered smoke lights" });
		auto scene = Scene::RandomScene(0, 1500, 164);
		std::vector<GpuLightHandle> handles;
		for (int i = 0; i < 2; ++i)
		{
			LightDesc sun;
			sun.Type = LightType::Directional;
			sun.Direction = { 0.3f * float(i) - 0.2f, -1.0f, 0.4f };
			sun.Intensity = 0.7f;
			lights.Create(sun);
		}
		for (const auto& desc : scene.Descs)
		{
			handles.push_back(lights.Create(desc));
		}

		std::mt19937 random(165);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);

		const auto frame = [&](const char* label, const ClusterGridDesc& gridDesc)
		{
			const auto view = Scene::Camera(float(gridDesc.ViewportWidth) / float(gridDesc.ViewportHeight));
			const std::uint32_t width = gridDesc.ViewportWidth;
			const std::uint32_t height = gridDesc.ViewportHeight;
			RenderGraph graph;
			const auto lightResources = lights.Import(graph);
			const auto clusters = assigner.Record(graph, lightResources, gridDesc, view);
			const auto& grid = clusters.GridRecord;

			// Synthetic reverse-Z depth: a sloped, rippled surface; the left 16 columns are sky.
			Rhi::TextureDesc depthDesc;
			depthDesc.Extent = { width, height, 1 };
			depthDesc.PixelFormat = Rhi::Format::R32Float;
			depthDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			depthDesc.DebugName = "Synthetic depth";
			const auto depth = graph.CreateTexture(depthDesc);
			std::vector<float> depthTexels(std::size_t(width) * height);
			for (std::uint32_t y = 0; y < height; ++y)
			{
				for (std::uint32_t x = 0; x < width; ++x)
				{
					const float viewDepth = 4.0f + 45.0f * (1.0f - float(y) / float(height)) + 3.0f * std::sin(float(x) * 0.05f);
					depthTexels[std::size_t(y) * width + x] = x < 16 ? 0.0f : grid.DepthParams[1] / viewDepth;
				}
			}
			AddTextureUpload(graph, "Depth upload", std::as_bytes(std::span(depthTexels)), depth, { 0, {}, {}, { width, height, 1 } });
			const auto heatmapTexture = assigner.RecordHeatmap(graph, clusters, depth);

			// Probe samples at random pixels and depths.
			constexpr std::uint32_t sampleCount = 1024;
			std::vector<ProbeSample> samples(sampleCount);
			for (auto& s : samples)
			{
				const float px = unit(random) * float(width);
				const float py = unit(random) * float(height);
				const float d = 0.5f + 40.0f * unit(random);
				const auto world = Scene::ViewToWorld(grid, Scene::ViewPoint(grid, px, py, d));
				const auto n = StandardPbr::Normalize({ unit(random) - 0.5f, 1.0f, unit(random) - 0.5f });
				const auto v = StandardPbr::Normalize({ unit(random) - 0.5f, 0.5f, 1.0f });
				s = { { world[0], world[1], world[2] }, unit(random), { n[0], n[1], n[2] }, 0.1f + 0.9f * unit(random),
					{ v[0], v[1], v[2] }, d, { unit(random), unit(random), unit(random) }, 0, { px, py }, { 0, 0 } };
			}
			const auto sampleUpload = graph.CreateUpload(std::as_bytes(std::span(samples)), "Probe samples", Rhi::BufferUsage::Storage, 16);
			const auto results = graph.CreateBuffer({ sampleCount * 32, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
				Rhi::MemoryPreference::DeviceLocal, "Probe results" });
			graph.AddPass(
				"Clustered probe", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(clusters.Grid, S::ShaderRead);
					b.Read(lightResources.Lights, S::ShaderRead);
					b.Read(lightResources.Header, S::ShaderRead);
					b.Read(clusters.Records, S::ShaderRead);
					b.Read(clusters.Indices, S::ShaderRead);
					b.Read(sampleUpload, S::ShaderRead);
					b.Write(results, S::ShaderWrite);
				},
				[&](RenderCommandContext& c)
				{
					auto table = c.Device().CreateDescriptorTable({ probe.Layout.get(), 0, 0, "Clustered probe table" });
					SWIM_REQUIRE(table);
					const std::array<GraphBuffer, 7> buffers{ clusters.Grid, lightResources.Lights, lightResources.Header, clusters.Records,
						clusters.Indices, sampleUpload, results };
					std::array<Rhi::DescriptorWrite, 7> writes{};
					for (std::uint32_t binding = 0; binding < writes.size(); ++binding)
					{
						const auto range = c.GetRange(buffers[binding]);
						writes[binding] = { binding, 0, range.Buffer, nullptr, nullptr, range.Offset, range.Size };
					}
					table->Write(writes);
					auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
					const std::array<std::uint32_t, 4> constants{ sampleCount, 0, 0, 0 };
					auto& list = c.Commands();
					list.BindComputePipeline(*probe.Pipeline);
					list.BindDescriptorTable(0, retained);
					list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
					list.Dispatch(sampleCount / 64, 1, 1);
				});

			const std::uint32_t clusterCount = clusters.Layout.ClusterCount;
			const auto viewLightsReadback =
				AddBufferReadback(graph, "View lights", clusters.ViewLights, 0, std::uint64_t(clusters.LocalLightCapacity) * 16);
			const auto boundsReadback = AddBufferReadback(graph, "Bounds", clusters.Bounds, 0, std::uint64_t(clusterCount) * 32);
			const auto recordsReadback = AddBufferReadback(graph, "Records", clusters.Records, 0, std::uint64_t(clusterCount) * 16);
			const std::uint64_t maskBufferWords = std::uint64_t(clusterCount) * ClusterBlockWords(grid);
			const auto indicesReadback = AddBufferReadback(graph, "Light masks", clusters.Indices, 0, maskBufferWords * 4);
			const auto statsReadback = AddBufferReadback(graph, "Stats", clusters.Stats, 0, sizeof(ClusterStats));
			const auto resultsReadback = AddBufferReadback(graph, "Probe", results, 0, sampleCount * 32);
			const auto heatmapReadback = AddTextureReadback(graph, "Heatmap", heatmapTexture, { 0, {}, {}, { width, height, 1 } });
			executor.Execute(graph.Compile());
			executor.Wait();
			lights.CommitUploads();

			const auto read = [&](const GraphReadback& readback, auto& target)
			{
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(target))) == Rhi::ReadbackStatus::Ready);
			};
			const auto& header = lights.GetHeader();
			std::vector<std::array<float, 4>> gpuViewLights(clusters.LocalLightCapacity);
			std::vector<std::array<float, 8>> gpuBounds(clusterCount);
			std::vector<ClusterRecord> gpuRecords(clusterCount);
			std::vector<std::uint32_t> gpuIndices(maskBufferWords);
			ClusterStats gpuStats{};
			std::vector<std::array<float, 4>> gpuResults(sampleCount * 2);
			std::vector<std::uint8_t> gpuHeatmap(std::size_t(width) * height * 4);
			read(viewLightsReadback, gpuViewLights);
			read(boundsReadback, gpuBounds);
			read(recordsReadback, gpuRecords);
			read(indicesReadback, gpuIndices);
			SWIM_REQUIRE(
				executor.TryReadback(statsReadback.Buffer, std::as_writable_bytes(std::span(&gpuStats, 1))) == Rhi::ReadbackStatus::Ready);
			read(resultsReadback, gpuResults);
			read(heatmapReadback, gpuHeatmap);

			// 1. View lights and cluster bounds.
			const auto rows = lights.GetRecords();
			std::vector<Cl::ViewLight> viewLights(header.LocalCount);
			std::uint32_t cullDifferences = 0;
			for (std::uint32_t i = 0; i < header.LocalCount; ++i)
			{
				const auto expected = Cl::CullLight(grid, rows[header.FirstLocalRow + i]);
				const auto& actual = gpuViewLights[i];
				viewLights[i] = { { actual[0], actual[1], actual[2] }, actual[3] };
				if ((expected.Radius >= 0.0f) != (actual[3] >= 0.0f))
				{
					++cullDifferences; // Allowed only when the sphere grazes the volume.
					const auto sphere = Lights::LightBoundingSphere(rows[header.FirstLocalRow + i]);
					const float gap = std::sqrt(Cl::DistanceSquaredToAabb(expected.Center, Cl::VolumeBounds(grid))) - sphere.Radius;
					SWIM_CHECK(std::abs(gap) < 1.0e-3f * (1.0f + sphere.Radius));
					continue;
				}
				for (int c = 0; c < 3; ++c)
				{
					SWIM_CHECK(std::abs(actual[c] - expected.Center[c]) < 1.0e-4f * (1.0f + std::abs(expected.Center[c])));
				}
				SWIM_CHECK(actual[3] < 0.0f || std::abs(actual[3] - expected.Radius) < 1.0e-5f * (1.0f + expected.Radius));
			}
			std::vector<Cl::Aabb> gpuAabbs(clusterCount);
			for (std::uint32_t c = 0; c < clusterCount; ++c)
			{
				const auto& b = gpuBounds[c];
				gpuAabbs[c] = { { b[0], b[1], b[2] }, { b[4], b[5], b[6] } };
				const auto expected = Cl::ClusterBounds(grid, c);
				for (int k = 0; k < 3; ++k)
				{
					SWIM_CHECK(std::abs(gpuAabbs[c].Min[k] - expected.Min[k]) < 1.0e-4f * (1.0f + std::abs(expected.Min[k])));
					SWIM_CHECK(std::abs(gpuAabbs[c].Max[k] - expected.Max[k]) < 1.0e-4f * (1.0f + std::abs(expected.Max[k])));
				}
			}

			// 2. Lists and stats against the CPU assignment over the GPU's inputs.
			const auto reference = Cl::AssignLights(grid, viewLights, gpuAabbs);
			std::uint32_t listDifferences = 0;
			Cl::ClusterAssignment gpuAssignment;
			gpuAssignment.Records = gpuRecords;
			gpuAssignment.Indices = gpuIndices;
			for (std::uint32_t c = 0; c < clusterCount; ++c)
			{
				const auto& actual = gpuRecords[c];
				const auto& expected = reference.Records[c];
				SWIM_CHECK_EQUAL(actual.Offset, expected.Offset);
				SWIM_CHECK_EQUAL(actual.Count, actual.RawCount); // Never truncated.
				const auto gpuList = Cl::ClusterLightList(gpuAssignment, grid, c);
				const auto cpuList = Cl::ClusterLightList(reference, grid, c);
				SWIM_CHECK_EQUAL(std::uint32_t(gpuList.size()), actual.Count);
				if (actual.RawCount == expected.RawCount && gpuList == cpuList)
				{
					continue;
				}
				++listDifferences;
				// Every light in only one of the lists grazes the cluster within float rounding.
				std::set<std::uint32_t> onlyOne;
				std::set_symmetric_difference(
					gpuList.begin(), gpuList.end(), cpuList.begin(), cpuList.end(), std::inserter(onlyOne, onlyOne.begin()));
				for (const auto light : onlyOne)
				{
					const auto& v = viewLights[light];
					const float gap = std::sqrt(Cl::DistanceSquaredToAabb(v.Center, gpuAabbs[c])) - v.Radius;
					SWIM_CHECK(std::abs(gap) < 1.0e-3f * (1.0f + v.Radius));
				}
			}
			SWIM_CHECK(listDifferences <= clusterCount / 200); // Grazing contacts only.
			SWIM_CHECK_EQUAL(gpuStats.ClusterCount, clusterCount);
			SWIM_CHECK_EQUAL(gpuStats.VisibleLights, reference.Stats.VisibleLights);
			SWIM_CHECK_EQUAL(gpuStats.DroppedIndices, 0u);
			SWIM_CHECK_EQUAL(gpuStats.WrittenIndices, gpuStats.RequestedIndices);
			if (listDifferences == 0)
			{
				SWIM_CHECK(std::memcmp(&gpuStats, &reference.Stats, sizeof(ClusterStats)) == 0);
			}

			// 3. Clustered shading equals brute force (when nothing was truncated) and the CPU model.
			std::uint32_t lit = 0;
			for (std::uint32_t i = 0; i < sampleCount; ++i)
			{
				const auto& s = samples[i];
				const auto& clustered = gpuResults[i * 2];
				const auto& brute = gpuResults[i * 2 + 1];
				StandardPbr::Surface surface;
				surface.BaseColor = { s.BaseColor[0], s.BaseColor[1], s.BaseColor[2] };
				surface.Metallic = s.Metallic;
				surface.PerceptualRoughness = s.Roughness;
				const auto cpu = Lights::ShadeAllLights(rows, header, surface, { s.Normal[0], s.Normal[1], s.Normal[2] },
					{ s.View[0], s.View[1], s.View[2] }, { s.Position[0], s.Position[1], s.Position[2] });
				const auto cluster = static_cast<std::uint32_t>(clustered[3]);
				SWIM_REQUIRE(cluster < clusterCount);
				for (int c = 0; c < 3; ++c)
				{
					SWIM_CHECK(std::abs(brute[c] - cpu[c]) <= 1.0e-4f + 2.0e-3f * std::abs(cpu[c]));
					SWIM_CHECK(std::abs(clustered[c] - brute[c]) <= 1.0e-4f + 1.0e-3f * std::abs(brute[c])); // Complete lists.
				}
				lit += brute[0] > 0.0f ? 1u : 0u;
			}
			SWIM_CHECK(lit > sampleCount / 4);

			// 4. The heatmap against HeatmapPixel over the GPU's records.
			std::uint32_t heatmapDifferences = 0;
			std::uint32_t magenta = 0;
			std::uint32_t colored = 0;
			for (std::uint32_t y = 0; y < height; ++y)
			{
				for (std::uint32_t x = 0; x < width; ++x)
				{
					const auto expected =
						Cl::HeatmapPixel(grid, gpuRecords, float(x) + 0.5f, float(y) + 0.5f, depthTexels[std::size_t(y) * width + x]);
					const auto* actual = &gpuHeatmap[(std::size_t(y) * width + x) * 4];
					bool same = true;
					for (int c = 0; c < 4; ++c)
					{
						same = same && std::abs(int(actual[c]) - int(std::lround(expected[c] * 255.0f))) <= 1;
					}
					heatmapDifferences += same ? 0u : 1u; // Slice-boundary pixels may pick the neighbor cluster.
					magenta += actual[0] == 255 && actual[1] == 0 && actual[2] == 255 ? 1u : 0u;
					colored += actual[3] != 0 ? 1u : 0u;
					if (x < 16)
					{
						SWIM_CHECK(actual[3] == 0); // Sky.
					}
				}
			}
			SWIM_CHECK(heatmapDifferences <= width * height / 200);
			SWIM_CHECK(colored > width * height / 4);

			std::printf("             [clusters %s] %ux%u, %u clusters, %u/%u lights visible, %u indices written of %u requested "
						"(%u dropped), %u overflowing clusters, max %u lights, %u non-empty; %u cull / %u list / %u heatmap "
						"float-edge differences; %u magenta pixels\n",
				label, width, height, clusterCount, gpuStats.VisibleLights, header.LocalCount, gpuStats.WrittenIndices,
				gpuStats.RequestedIndices, gpuStats.DroppedIndices, gpuStats.OverflowClusters, gpuStats.MaxRawLightsPerCluster,
				gpuStats.NonEmptyClusters, cullDifferences, listDifferences, heatmapDifferences, magenta);
			return std::array<std::uint32_t, 3>{ gpuStats.RequestedIndices, gpuStats.OverflowClusters, magenta };
		};

		ClusterGridDesc gridDesc;
		gridDesc.ViewportWidth = 1280;
		gridDesc.ViewportHeight = 720;
		gridDesc.TileSize = 64;
		gridDesc.SliceCount = 16;
		gridDesc.Near = 0.1f;
		gridDesc.Far = 60.0f;
		gridDesc.MaxLightsPerCluster = 256;
		gridDesc.LightCapacity = 1024;
		const auto roomy = frame("roomy", gridDesc);
		SWIM_CHECK_EQUAL(roomy[1], 0u);

		// A tiny heatmap scale: clusters above it are counted, none is truncated (no magenta).
		auto tight = gridDesc;
		tight.MaxLightsPerCluster = 4;
		const auto dense = frame("dense", tight);
		SWIM_CHECK(dense[1] > 0u);
		SWIM_CHECK_EQUAL(dense[0], roomy[0]);
		SWIM_CHECK_EQUAL(dense[2], 0u);

		// Moved lights and a new resolution.
		for (std::size_t i = 0; i < 300; ++i)
		{
			auto desc = scene.Descs[i];
			desc.Position[0] += 2.0f;
			desc.Position[2] -= 3.0f;
			SWIM_CHECK(lights.Update(handles[i], desc));
		}
		auto resized = gridDesc;
		resized.ViewportWidth = 800;
		resized.ViewportHeight = 450;
		frame("resized", resized);
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ClusteredLightingMatchesTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunClusteredLightingSmoke);
				} });
		}
		return true;
	}();
} // namespace
