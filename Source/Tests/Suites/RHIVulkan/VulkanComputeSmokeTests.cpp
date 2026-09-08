#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_COMPUTE_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	void RunComputeSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_COMPUTE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Compute smoke requires SWIM_BUILD_SHADER_COMPILER=ON and generated Slang artifacts");
#else
		using namespace Swim;
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_COMPUTE_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), 1u);
		SWIM_REQUIRE_EQUAL(interface.PushConstants.size(), 1u);
		const auto& schema = interface.DescriptorSchemas[0];
		const auto bindingFor = [&](Rhi::DescriptorType type)
		{
			for (const auto& binding : schema.Bindings)
			{
				if (binding.Type == type)
				{
					return binding.Binding;
				}
			}
			throw std::runtime_error("Compute reflection is missing a buffer binding");
		};
		std::ifstream file(SWIM_RHI_COMPUTE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Compute smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Compute smoke program" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Compute smoke layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), "computeMain", "Compute smoke pipeline" });
		SWIM_REQUIRE(pipeline);
		constexpr std::uint32_t elementCount = 256;
		constexpr std::uint32_t activeCount = elementCount - 3;
		constexpr std::uint64_t bufferBytes = elementCount * sizeof(std::uint32_t);
		auto input = device->CreateBuffer({ bufferBytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
			Rhi::MemoryPreference::CpuToGpu, "Compute smoke input" });
		auto output = device->CreateBuffer({ bufferBytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::DeviceLocal, "Compute smoke output" });
		auto readback = device->CreateBuffer({ bufferBytes, Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::GpuToCpu, "Compute smoke readback" });
		SWIM_REQUIRE(input && output && readback);
		auto table = device->CreateDescriptorTable({ layout.get(), schema.Space });
		SWIM_REQUIRE(table);
		std::array<Rhi::DescriptorWrite, 2> writes{};
		writes[0].Binding = bindingFor(Rhi::DescriptorType::ReadOnlyStorageBuffer);
		writes[0].BufferResource = input.get();
		writes[1].Binding = bindingFor(Rhi::DescriptorType::StorageBuffer);
		writes[1].BufferResource = output.get();
		table->Write(writes);
		// All buffers stay on this family, whether compute aliases graphics or is dedicated.
		// Last owner drains before any referenced resource is destroyed on failure.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Compute, 2 });
		SWIM_REQUIRE(frames);
		std::array<std::uint32_t, elementCount> source{};
		std::array<std::uint32_t, elementCount> actual{};
		const auto readWrite = Rhi::ResourceState::ShaderRead | Rhi::ResourceState::ShaderWrite;
		for (std::uint32_t frame = 0; frame < 4; ++frame)
		{
			for (std::uint32_t index = 0; index < elementCount; ++index)
			{
				source[index] = index * 17 + frame * 101;
			}
			input->Write(0, std::as_bytes(std::span(source)));
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			commands.BeginDebugLabel("Compute smoke: two storage passes and readback");
			commands.Transition(*input, Rhi::ResourceState::HostWrite, Rhi::ResourceState::ShaderRead | Rhi::ResourceState::CopySource);
			commands.Transition(*output, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::CopySource, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*input, *output, { 0, 0, bufferBytes });
			commands.Transition(*output, Rhi::ResourceState::CopyDestination, readWrite);
			commands.BindComputePipeline(*pipeline);
			commands.BindDescriptorTable(schema.Space, *table);
			// Global extent is (2,2,2) * local (8,4,1) = (16,8,2).
			SWIM_REQUIRE((interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 8, 4, 1 }));
			std::array<std::uint32_t, 6> constants{ activeCount, 16, 8, 3, 7, 0 };
			SWIM_REQUIRE_EQUAL(interface.PushConstants[0].Size, sizeof(constants));
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
			commands.Dispatch(2, 2, 2);
			// Make the first pass's writes visible to the second pass's read-modify-write.
			commands.Transition(*output, readWrite, readWrite);
			constants[3] = 5;
			constants[4] = 11;
			constants[5] = 1;
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 12, std::as_bytes(std::span(constants).subspan(3)));
			commands.Dispatch(2, 2, 2);
			commands.Transition(*output, readWrite, Rhi::ResourceState::CopySource);
			commands.Transition(*readback, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*output, *readback, { 0, 0, bufferBytes });
			commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.EndDebugLabel();
			commands.End();
			frames->SubmitCurrent();
			frames->Drain();
			readback->Read(0, std::as_writable_bytes(std::span(actual)));
			for (std::uint32_t index = 0; index < elementCount; ++index)
			{
				const auto expected = index < activeCount ? source[index] * 8 + 18 : source[index];
				SWIM_CHECK_EQUAL(actual[index], expected);
			}
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ComputeStoragePassesAndReadback",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunComputeSmoke); } });
		}
		return true;
	}();

} // namespace
