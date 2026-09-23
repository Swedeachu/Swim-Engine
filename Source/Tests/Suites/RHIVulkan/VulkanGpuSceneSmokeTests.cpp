#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_GPU_SCENE_PROBE_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	// Critical-path items 46/48 on a real device: 100k persistent objects are
	// uploaded once, then frames change 1% of them. Each frame a compute probe
	// (Shaders/Slang/RhiSmoke/GpuSceneProbe.slang, which includes the shared
	// GpuSceneRecords.slang) reads every instance and transform row in the same
	// graph, and the results must match the CPU mirror: record layouts, dead
	// rows, drawability flags, previous transforms and dirty-only uploads.
	void RunGpuSceneSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_GPU_SCENE_PROBE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "GPU Scene smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;

		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_GPU_SCENE_PROBE_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), 1u);
		std::ifstream file(SWIM_RHI_GPU_SCENE_PROBE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file && !bytecode.empty());

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "GPU Scene smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "GPU Scene probe" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "GPU Scene probe layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "GPU Scene probe" });
		SWIM_REQUIRE(pipeline);
		const auto space = interface.DescriptorSchemas[0].Space;

		constexpr std::uint32_t count = 100000;
		RenderGraphExecutor executor(*device);
		{
			GpuScene scene(*device, { count, "Smoke GPU scene" });
			std::vector<RenderObjectHandle> objects;
			objects.reserve(count);
			for (std::uint32_t i = 0; i < count; ++i)
			{
				RenderObjectDesc desc;
				desc.Transform = RenderAffine::Translation(float(i % 500), float(i / 500), 0.0f);
				desc.Transform.Rows[0] = desc.Transform.Rows[5] = desc.Transform.Rows[10] = 1.0f + float(i % 3); // Uniform scale.
				desc.LocalBounds = { { 0.5f, float(i % 7), -1.0f }, { 1.0f, 1.0f, 1.0f } };
				desc.Mesh = i % 10 == 9 ? GpuMeshHandle{} : GpuMeshHandle{ i % 128, 1 }; // Every tenth has no mesh.
				desc.ObjectId = i;
				desc.MaterialSet = i % 32;
				objects.push_back(scene.Create(desc));
			}

			// Runs one frame: import (dirty rows), probe every row, read back, verify.
			const auto frame = [&](std::uint64_t expectedBytes)
			{
				RenderGraph graph;
				const auto resources = scene.Import(graph);
				SWIM_CHECK_EQUAL(resources.UploadBytes, expectedBytes);
				const auto rows = resources.RowCount;
				const std::uint64_t resultBytes = std::uint64_t(rows) * 32;
				const auto results = graph.CreateBuffer({ resultBytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
					Rhi::MemoryPreference::DeviceLocal, "Probe results" });
				graph.AddPass(
					"GPU Scene probe", Rhi::QueueType::Compute,
					[&](RenderGraphBuilder& b)
					{
						b.Read(resources.Instances, S::ShaderRead);
						b.Read(resources.Transforms, S::ShaderRead);
						b.Write(results, S::ShaderWrite);
					},
					[&, rows](RenderCommandContext& c)
					{
						auto table = c.Device().CreateDescriptorTable({ layout.get(), space, 0, "GPU Scene probe table" });
						SWIM_REQUIRE(table);
						std::array<Rhi::DescriptorWrite, 3> writes{};
						writes[0].Binding = 0;
						writes[0].BufferResource = &c.Get(resources.Instances);
						writes[1].Binding = 1;
						writes[1].BufferResource = &c.Get(resources.Transforms);
						writes[2].Binding = 2;
						writes[2].BufferResource = &c.Get(results);
						table->Write(writes);
						auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
						auto& commands = c.Commands();
						commands.BindComputePipeline(*pipeline);
						commands.BindDescriptorTable(space, retained);
						const std::array<std::uint32_t, 4> push{ rows, 0, 0, 0 };
						commands.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(push)));
						commands.Dispatch((rows + 63) / 64, 1, 1);
					});
				const auto readback = AddBufferReadback(graph, "Probe readback", results, 0, resultBytes);
				executor.Execute(graph.Compile());
				scene.CommitUploads();
				executor.Wait();

				std::vector<std::array<float, 4>> actual(std::size_t(rows) * 2);
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(actual))) == Rhi::ReadbackStatus::Ready);
				std::uint32_t mismatches = 0;
				for (std::uint32_t row = 0; row < rows; ++row)
				{
					const auto& instance = scene.GetInstanceRow(row);
					const auto& current = actual[std::size_t(row) * 2];
					const auto& previous = actual[std::size_t(row) * 2 + 1];
					if ((instance.Flags & std::uint32_t(RenderObjectFlags::Live)) == 0)
					{
						mismatches += current[3] != -1.0f || previous[3] != -1.0f;
						continue;
					}
					const auto& transform = scene.GetTransformRow(instance.TransformIndex);
					const std::array<float, 3> center{ instance.LocalCenter[0], instance.LocalCenter[1], instance.LocalCenter[2] };
					RenderAffine now;
					RenderAffine before;
					std::copy(std::begin(transform.Current), std::end(transform.Current), now.Rows.begin());
					std::copy(std::begin(transform.Previous), std::end(transform.Previous), before.Rows.begin());
					const auto a = now.TransformPoint(center);
					const auto b = before.TransformPoint(center);
					const std::uint32_t required =
						std::uint32_t(RenderObjectFlags::Live | RenderObjectFlags::HasMesh | RenderObjectFlags::Visible);
					const float drawable = (instance.Flags & required) == required ? 1.0f : 0.0f;
					for (int axis = 0; axis < 3; ++axis)
					{
						mismatches += std::abs(current[axis] - a[axis]) > 1.0e-3f || std::abs(previous[axis] - b[axis]) > 1.0e-3f;
					}
					mismatches += current[3] != drawable || previous[3] != float(instance.ObjectId);
				}
				SWIM_CHECK_EQUAL(mismatches, 0u);
				return resources;
			};

			// Frame 1: everything.
			frame(std::uint64_t(count) * (sizeof(GpuInstanceRecord) + sizeof(GpuTransformRecord)));

			// Frame 2: 1% move, 100 destroyed, 50 hidden.
			for (std::uint32_t i = 3; i < count; i += 100)
			{
				scene.SetTransform(objects[i], RenderAffine::Translation(float(i), -5.0f, 2.0f));
			}
			Rhi::TimelinePoint none{};
			for (std::uint32_t i = 50; i < count; i += 1000)
			{
				SWIM_CHECK(scene.Destroy(objects[i], none));
			}
			for (std::uint32_t i = 7; i < 50 * 97; i += 97)
			{
				scene.SetFlags(objects[i], RenderObjectFlags::CastShadows);
			}
			frame(1000 * sizeof(GpuTransformRecord) + 150 * sizeof(GpuInstanceRecord));

			// Frame 3: no edits; the 1000 moved objects settle (Previous = Current).
			const auto settled = frame(1000 * sizeof(GpuTransformRecord));
			SWIM_CHECK_EQUAL(settled.InstanceRows, 0u);
			// Frame 4: fully static.
			const auto idle = frame(0);
			SWIM_CHECK(idle.UploadPasses.empty());

			SWIM_CHECK_EQUAL(scene.Collect(), 100u);
			SWIM_CHECK_EQUAL(scene.GetStats().LiveObjects, count - 100);
			scene.Drain();
		}
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "GpuSceneHundredThousandObjectsDirtyUploads", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunGpuSceneSmoke);
				} });
		}
		return true;
	}();

} // namespace
