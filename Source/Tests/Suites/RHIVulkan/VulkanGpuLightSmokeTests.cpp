#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_GPU_LIGHT_PROBE_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_RHI_GPU_LIGHT_PROBE_SPIRV_PATH
	// Mirrors GpuLightProbe.slang's LightProbeSample (std430, 64 bytes).
	struct ProbeSample
	{
		float Position[3];
		float Metallic;
		float Normal[3];
		float Roughness;
		float View[3];
		float Reserved0;
		float BaseColor[3];
		float Reserved1;
	};

	static_assert(sizeof(ProbeSample) == 64);
#endif

	// Critical-path item 63 on a real device. A GpuLightBuffer holding 2 directional
	// and 2,000 point/spot lights is uploaded through the render graph, and a compute
	// probe shades 512 sample points by summing StandardPbr over every light (the
	// brute-force forward reference clustered lighting must later reproduce). Each
	// frame compares every sample with Lights::ShadeAllLights over the CPU mirror:
	//  1. the first upload (every row plus the header);
	//  2. churn: 300 releases (swap-removes), 200 edits including type changes, 100
	//     new lights; only the touched rows upload;
	//  3. no changes: no upload passes, identical results.
	void RunGpuLightSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_GPU_LIGHT_PROBE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "GPU light smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;

		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_GPU_LIGHT_PROBE_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		std::ifstream file(SWIM_RHI_GPU_LIGHT_PROBE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file && !bytes.empty());

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "GPU light smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const auto& programInterface = converted.Interface;
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytes };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ programInterface.DescriptorSchemas, programInterface.PushConstants, programInterface.ComputeThreadGroupSize },
			"GPU light probe" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "GPU light probe" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "GPU light probe" });
		SWIM_REQUIRE(pipeline);

		RenderGraphExecutor executor(*device);
		GpuLightBuffer lights(*device, { 4, 4096, "Smoke lights" });
		std::mt19937 random(163);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		std::normal_distribution<float> normal(0.0f, 1.0f);
		const auto randomDirection = [&]()
		{
			return std::array<float, 3>{ normal(random), normal(random), normal(random) };
		};
		const auto randomLight = [&](bool directional)
		{
			LightDesc desc;
			desc.Color = { 0.2f + unit(random), 0.2f + unit(random), 0.2f + unit(random) };
			if (directional)
			{
				desc.Type = LightType::Directional;
				desc.Direction = randomDirection();
				desc.Intensity = 0.5f + unit(random);
				return desc;
			}
			desc.Type = unit(random) < 0.5f ? LightType::Point : LightType::Spot;
			desc.Position = { 40 * unit(random) - 20, 10 * unit(random), 40 * unit(random) - 20 };
			desc.Direction = randomDirection();
			desc.Intensity = 1.0f + 20.0f * unit(random);
			desc.Range = 2.0f + 6.0f * unit(random);
			desc.OuterConeAngle = 0.2f + 1.2f * unit(random);
			desc.InnerConeAngle = desc.OuterConeAngle * unit(random) * 0.8f;
			return desc;
		};
		std::vector<GpuLightHandle> handles;
		for (int i = 0; i < 2; ++i)
		{
			handles.push_back(lights.Create(randomLight(true)));
		}
		for (int i = 0; i < 2000; ++i)
		{
			handles.push_back(lights.Create(randomLight(false)));
		}

		// Sample points on and above a 40 x 40 m floor with assorted materials.
		constexpr std::uint32_t sampleCount = 512;
		std::vector<ProbeSample> samples(sampleCount);
		for (auto& s : samples)
		{
			const auto n = StandardPbr::Normalize({ 0.3f * normal(random), 1.0f, 0.3f * normal(random) });
			const auto v = StandardPbr::Normalize({ normal(random), 1.0f + unit(random), normal(random) });
			s = { { 40 * unit(random) - 20, 3 * unit(random), 40 * unit(random) - 20 }, unit(random), { n[0], n[1], n[2] },
				0.1f + 0.9f * unit(random), { v[0], v[1], v[2] }, 0, { unit(random), unit(random), unit(random) }, 0 };
		}

		const auto frame = [&](const char* label, bool expectUpload)
		{
			RenderGraph graph;
			const auto resources = lights.Import(graph);
			SWIM_CHECK(resources.UploadPass.has_value() == expectUpload);
			const auto sampleUpload =
				graph.CreateUpload(std::as_bytes(std::span(samples)), "Light probe samples", Rhi::BufferUsage::Storage, 16);
			const auto results = graph.CreateBuffer({ sampleCount * 16, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
				Rhi::MemoryPreference::DeviceLocal, "Light probe results" });
			graph.AddPass(
				"Light probe", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(resources.Lights, S::ShaderRead);
					b.Read(resources.Header, S::ShaderRead);
					b.Read(sampleUpload, S::ShaderRead);
					b.Write(results, S::ShaderWrite);
				},
				[&](RenderCommandContext& c)
				{
					auto table = c.Device().CreateDescriptorTable({ layout.get(), 0, 0, "Light probe table" });
					SWIM_REQUIRE(table);
					std::array<Rhi::DescriptorWrite, 4> writes{};
					for (std::uint32_t binding = 0; binding < writes.size(); ++binding)
					{
						writes[binding].Binding = binding;
					}
					writes[0].BufferResource = &c.Get(resources.Lights);
					writes[1].BufferResource = &c.Get(resources.Header);
					const auto range = c.GetRange(sampleUpload);
					writes[2].BufferResource = range.Buffer;
					writes[2].BufferOffset = range.Offset;
					writes[2].BufferRange = range.Size;
					writes[3].BufferResource = &c.Get(results);
					table->Write(writes);
					auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
					const std::array<std::uint32_t, 4> constants{ sampleCount, 0, 0, 0 };
					auto& list = c.Commands();
					list.BindComputePipeline(*pipeline);
					list.BindDescriptorTable(0, retained);
					list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
					list.Dispatch(sampleCount / 64, 1, 1);
				});
			const auto readback = AddBufferReadback(graph, "Light probe readback", results, 0, sampleCount * 16);
			executor.Execute(graph.Compile());
			executor.Wait();
			lights.CommitUploads();
			std::vector<std::array<float, 4>> actual(sampleCount);
			SWIM_REQUIRE(executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(actual))) == Rhi::ReadbackStatus::Ready);

			const auto& header = lights.GetHeader();
			float worst = 0.0f;
			std::uint32_t lit = 0;
			for (std::uint32_t i = 0; i < sampleCount; ++i)
			{
				const auto& s = samples[i];
				StandardPbr::Surface surface;
				surface.BaseColor = { s.BaseColor[0], s.BaseColor[1], s.BaseColor[2] };
				surface.Metallic = s.Metallic;
				surface.PerceptualRoughness = s.Roughness;
				const auto expected =
					Lights::ShadeAllLights(lights.GetRecords(), header, surface, { s.Normal[0], s.Normal[1], s.Normal[2] },
						{ s.View[0], s.View[1], s.View[2] }, { s.Position[0], s.Position[1], s.Position[2] });
				for (int c = 0; c < 3; ++c)
				{
					const float error = std::abs(actual[i][c] - expected[c]);
					worst = std::max(worst, error / (std::abs(expected[c]) + 1.0e-3f));
					SWIM_CHECK(error <= 1.0e-4f + 2.0e-3f * std::abs(expected[c]));
				}
				SWIM_CHECK_EQUAL(actual[i][3], float(header.DirectionalCount + header.LocalCount));
				lit += expected[0] > 0.0f ? 1u : 0u;
			}
			const auto stats = lights.GetStats();
			std::printf("             [lights %s] %u directional + %u local, uploaded %u rows in %u runs (%llu bytes, header %d), "
						"worst relative error %.2e, %u/%u samples lit\n",
				label, header.DirectionalCount, header.LocalCount, stats.LastUploadRows, stats.LastUploadRuns,
				static_cast<unsigned long long>(stats.LastUploadBytes), stats.LastUploadHeader ? 1 : 0, worst, lit, sampleCount);
			SWIM_CHECK(lit > sampleCount / 2);
			return stats;
		};

		// 1. First upload: every live row (two runs) and the header.
		auto stats = frame("initial", true);
		SWIM_CHECK_EQUAL(stats.LastUploadRows, 2002u);
		SWIM_CHECK_EQUAL(stats.LastUploadRuns, 2u);
		SWIM_CHECK(stats.LastUploadHeader);

		// 2. Churn.
		std::uint32_t moved = 0;
		for (int i = 0; i < 300; ++i)
		{
			const auto index = 2 + std::size_t(unit(random) * float(handles.size() - 2)) % (handles.size() - 2);
			SWIM_CHECK(lights.Release(handles[index]));
			handles.erase(handles.begin() + std::ptrdiff_t(index));
		}
		for (int i = 0; i < 200; ++i)
		{
			const auto index = 2 + std::size_t(unit(random) * float(handles.size() - 2)) % (handles.size() - 2);
			const bool toDirectional = i < 2; // Two lights become directional (fills the range to 4).
			moved += toDirectional ? 1u : 0u;
			SWIM_CHECK(lights.Update(handles[index], randomLight(toDirectional)));
		}
		for (int i = 0; i < 100; ++i)
		{
			handles.push_back(lights.Create(randomLight(false)));
		}
		std::uint32_t directionalLights = 0;
		for (const auto handle : handles)
		{
			directionalLights += lights.Find(handle)->Type == LightType::Directional ? 1u : 0u;
		}
		SWIM_CHECK_EQUAL(lights.GetHeader().DirectionalCount, directionalLights);
		SWIM_CHECK_EQUAL(lights.GetHeader().LocalCount, std::uint32_t(handles.size()) - directionalLights);
		SWIM_CHECK(directionalLights > 2u);
		stats = frame("churn", true);
		SWIM_CHECK(stats.LastUploadRows <= 300u + 200u + 100u + 2u * moved);
		SWIM_CHECK(stats.LastUploadRows < 2002u);
		SWIM_CHECK(stats.LastUploadHeader);

		// 3. Nothing changed: no uploads.
		stats = frame("steady", false);
		SWIM_CHECK_EQUAL(stats.LastUploadRows, 0u);
		SWIM_CHECK(!stats.LastUploadHeader);
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "GpuLightBufferMatchesBruteForceShading", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunGpuLightSmoke);
				} });
		}
		return true;
	}();
} // namespace
