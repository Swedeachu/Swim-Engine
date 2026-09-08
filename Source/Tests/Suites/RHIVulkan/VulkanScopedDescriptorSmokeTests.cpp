#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_SCOPED_COMPUTE_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	void RunScopedDescriptorSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_SCOPED_COMPUTE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Scoped descriptor smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_SCOPED_COMPUTE_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), 2u);
		SWIM_REQUIRE_EQUAL(interface.PushConstants.size(), 1u);
		SWIM_REQUIRE_EQUAL(interface.PushConstants[0].Size, 16u);
		SWIM_REQUIRE((interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 8, 1, 1 }));
		std::ifstream file(SWIM_RHI_SCOPED_COMPUTE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Scoped descriptor smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Scoped compute program" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Scoped compute layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "Scoped compute pipeline" });
		SWIM_REQUIRE(pipeline);
		constexpr std::uint32_t count = 64;
		constexpr std::uint32_t active = count - 3;
		constexpr std::uint64_t bytes = count * sizeof(std::uint32_t);
		auto input = device->CreateBuffer({ bytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
			Rhi::MemoryPreference::CpuToGpu, "Scoped compute input" });
		auto output = device->CreateBuffer({ bytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::DeviceLocal, "Scoped compute output" });
		auto parameters = device->CreateBuffer({ 16, Rhi::BufferUsage::Uniform,
			Rhi::MemoryPreference::CpuToGpu, "Scoped compute uniform parameters" });
		auto readback = device->CreateBuffer({ bytes, Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::GpuToCpu, "Scoped compute readback" });
		SWIM_REQUIRE(input && output && parameters && readback);
		std::array<std::unique_ptr<Rhi::DescriptorTable>, 2> tables;
		for (std::size_t index = 0; index < tables.size(); ++index)
		{
			const auto& schema = interface.DescriptorSchemas[index];
			tables[index] = device->CreateDescriptorTable({ layout.get(), schema.Space });
			SWIM_REQUIRE(tables[index]);
			std::vector<Rhi::DescriptorWrite> writes;
			for (const auto& binding : schema.Bindings)
			{
				SWIM_REQUIRE_EQUAL(binding.Stages, Rhi::ShaderStageMask::Compute);
				Rhi::DescriptorWrite write;
				write.Binding = binding.Binding;
				switch (binding.Type)
				{
				case Rhi::DescriptorType::ReadOnlyStorageBuffer: write.BufferResource = input.get(); break;
				case Rhi::DescriptorType::StorageBuffer: write.BufferResource = output.get(); break;
				case Rhi::DescriptorType::UniformBuffer: write.BufferResource = parameters.get(); break;
				default: throw std::runtime_error("Unexpected scoped compute descriptor type");
				}
				writes.push_back(write);
			}
			tables[index]->Write(writes);
		}
		// Resources stay on one compute-capable family. Drain before host writes
		// and retain the frame ring last so exceptions cannot destroy live resources.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Compute, 2 });
		SWIM_REQUIRE(frames);
		std::array<std::uint32_t, count> source{};
		std::array<std::uint32_t, count> actual{};
		const auto rw = Rhi::ResourceState::ShaderRead | Rhi::ResourceState::ShaderWrite;
		for (std::uint32_t frame = 0; frame < 4; ++frame)
		{
			for (std::uint32_t index = 0; index < count; ++index)
			{
				source[index] = frame * 101 + index;
			}
			const std::array<std::uint32_t, 4> uniform{ active, 3 + frame, 7 + frame, 0 };
			input->Write(0, std::as_bytes(std::span(source)));
			parameters->Write(0, std::as_bytes(std::span(uniform)));
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			commands.Transition(*input, Rhi::ResourceState::HostWrite, Rhi::ResourceState::ShaderRead | Rhi::ResourceState::CopySource);
			commands.Transition(*parameters, Rhi::ResourceState::HostWrite, Rhi::ResourceState::UniformBuffer);
			commands.Transition(*output, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::CopySource, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*input, *output, { 0, 0, bytes });
			commands.Transition(*output, Rhi::ResourceState::CopyDestination, rw);
			commands.BindComputePipeline(*pipeline);
			for (std::size_t index = 0; index < tables.size(); ++index)
			{
				commands.BindDescriptorTable(interface.DescriptorSchemas[index].Space, *tables[index]);
			}
			std::array<std::uint32_t, 4> push{ active, 0, 11 + frame, 0 };
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(push)));
			commands.Dispatch(8, 1, 1);
			commands.Transition(*output, rw, rw);
			push[3] = 1;
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 12, std::as_bytes(std::span(push).subspan(3)));
			commands.Dispatch(8, 1, 1);
			commands.Transition(*output, rw, Rhi::ResourceState::CopySource);
			commands.Transition(*readback, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*output, *readback, { 0, 0, bytes });
			commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.End();
			frames->SubmitCurrent();
			frames->Drain();
			readback->Read(0, std::as_writable_bytes(std::span(actual)));
			for (std::uint32_t index = 0; index < count; ++index)
			{
				SWIM_CHECK_EQUAL(actual[index], index < active ? source[index] * uniform[1] + uniform[2] + push[2] : source[index]);
			}
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ScopedComputeDescriptorsAndReadback",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunScopedDescriptorSmoke); } });
		}
		return true;
	}();

} // namespace
