#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"
#ifdef SWIM_RHI_COMPUTE_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif
#include <array>
#include <cstdlib>
#include <fstream>

namespace
{
	void RunGraphComputeSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_COMPUTE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "RenderGraph compute smoke requires generated Slang compute artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;
		using Q = Rhi::QueueType;

		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_COMPUTE_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), 1u);
		const auto& schema = interface.DescriptorSchemas[0];
		std::uint32_t inputBinding = UINT32_MAX, outputBinding = UINT32_MAX;
		for (const auto& binding : schema.Bindings)
		{
			if (binding.Type == Rhi::DescriptorType::ReadOnlyStorageBuffer)
			{
				inputBinding = binding.Binding;
			}
			if (binding.Type == Rhi::DescriptorType::StorageBuffer)
			{
				outputBinding = binding.Binding;
			}
		}
		SWIM_REQUIRE(inputBinding != UINT32_MAX && outputBinding != UINT32_MAX);

		std::ifstream file(SWIM_RHI_COMPUTE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);

		Platform::PlatformSystem platform;
		SWIM_REQUIRE(platform.Initialize());
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram(
			{ { &stage, 1 }, { interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Graph compute" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Graph compute layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), "computeMain", "Graph compute pipeline" });
		SWIM_REQUIRE(pipeline);

		constexpr std::uint32_t count = 256, active = count - 3;
		constexpr std::uint64_t bufferBytes = count * sizeof(std::uint32_t);
		auto input = device->CreateBuffer(
			{ bufferBytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Graph input" });
		auto readback =
			device->CreateBuffer({ bufferBytes, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Graph results" });
		SWIM_REQUIRE(input && readback);

		RenderGraphExecutor executor(*device);
		for (std::uint32_t frame = 0; frame < 4; ++frame)
		{
			executor.Wait();
			std::array<std::uint32_t, count> source{}, actual{};
			for (std::uint32_t i = 0; i < count; ++i)
			{
				source[i] = i * 17 + frame * 101;
			}
			input->Write(0, std::as_bytes(std::span(source)));

			RenderGraph graph;
			auto sourceBuffer = graph.ImportBuffer(*input, S::HostWrite);
			auto destination = graph.ImportBuffer(*readback, frame ? S::HostRead : S::Undefined);
			auto data = graph.CreateBuffer(
				{ bufferBytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
					Rhi::MemoryPreference::DeviceLocal, "Graph working data" });

			graph.AddPass(
				"Initialize", Q::Transfer,
				[&](auto& b)
				{
					b.Read(sourceBuffer, S::CopySource);
					b.Write(data, S::CopyDestination);
				},
				[&](auto& c)
				{
					c.Commands().CopyBuffer(c.Get(sourceBuffer), c.Get(data), { 0, 0, bufferBytes });
				});

			for (std::uint32_t step = 0; step < 2; ++step)
			{

				graph.AddPass(
					step ? "Accumulate" : "Multiply", Q::Compute,
					[&](auto& b)
					{
						b.Read(sourceBuffer, S::ShaderRead);
						b.ReadWrite(data, S::ShaderRead | S::ShaderWrite);
					},
					[&, step](RenderCommandContext& c)
					{
						auto table = c.Device().CreateDescriptorTable({ layout.get(), schema.Space, 0, "Graph compute table" });
						SWIM_REQUIRE(table);
						std::array<Rhi::DescriptorWrite, 2> writes{};
						writes[0].Binding = inputBinding;
						writes[0].BufferResource = &c.Get(sourceBuffer);
						writes[1].Binding = outputBinding;
						writes[1].BufferResource = &c.Get(data);
						table->Write(writes);
						auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));

						auto& commands = c.Commands();
						commands.BindComputePipeline(*pipeline);
						commands.BindDescriptorTable(schema.Space, retained);

						std::array<std::uint32_t, 6> constants{ active, 16, 8, step ? 5u : 3u, step ? 11u : 7u, step };
						commands.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
						commands.Dispatch(2, 2, 2);
					});
			}

			graph.AddPass(
				"Readback", Q::Transfer,
				[&](auto& b)
				{
					b.Read(data, S::CopySource);
					b.Write(destination, S::CopyDestination);
				},
				[&](auto& c)
				{
					c.Commands().CopyBuffer(c.Get(data), c.Get(destination), { 0, 0, bufferBytes });
				});

			graph.Export(destination, S::HostRead);
			auto plan = graph.Compile();
			SWIM_REQUIRE_EQUAL(plan.GetSchedule().size(), 4u);
			const auto& barriers = plan.GetSchedule()[2].Barriers;
			SWIM_REQUIRE_EQUAL(barriers.size(), 1u);
			SWIM_CHECK_EQUAL(barriers[0].Before, barriers[0].After);

			executor.Execute(plan);
			executor.Wait();

			readback->Read(0, std::as_writable_bytes(std::span(actual)));
			for (std::uint32_t i = 0; i < count; ++i)
			{
				SWIM_CHECK_EQUAL(actual[i], i < active ? source[i] * 8 + 18 : source[i]);
			}
			SWIM_CHECK_EQUAL(executor.GetPooledResourceCount(), 1u);
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "RenderGraphComputeStorageAndReadback", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunGraphComputeSmoke);
				} });
		}
		return true;
	}();
} // namespace
