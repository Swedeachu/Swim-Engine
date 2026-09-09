#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_DESCRIPTOR_ARRAYS_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	void RunDescriptorArraySmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_DESCRIPTOR_ARRAYS_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Descriptor array smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_DESCRIPTOR_ARRAYS_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), 2u);
		SWIM_REQUIRE_EQUAL(interface.PushConstants.size(), 1u);
		SWIM_REQUIRE_EQUAL(interface.PushConstants[0].Size, 16u);
		SWIM_REQUIRE((interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 8, 1, 1 }));
		std::ifstream file(SWIM_RHI_DESCRIPTOR_ARRAYS_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Descriptor array smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Descriptor arrays" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Descriptor array layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "Descriptor array compute" });
		SWIM_REQUIRE(pipeline);
		constexpr std::uint32_t count = 64;
		constexpr std::uint32_t active = count - 3;
		constexpr std::uint64_t bytes = count * sizeof(std::uint32_t);
		std::array<std::unique_ptr<Rhi::Buffer>, 2> inputs, parameters, outputs;
		std::array<std::unique_ptr<Rhi::Texture>, 2> images, storage;
		std::array<std::unique_ptr<Rhi::TextureView>, 2> imageViews, storageViews;
		std::array<std::unique_ptr<Rhi::Sampler>, 2> samplers;
		for (std::size_t index = 0; index < 2; ++index)
		{
			inputs[index] = device->CreateBuffer({ bytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
				Rhi::MemoryPreference::CpuToGpu, "Array input" });
			parameters[index] = device->CreateBuffer({ 16, Rhi::BufferUsage::Uniform, Rhi::MemoryPreference::CpuToGpu, "Array parameters" });
			outputs[index] = device->CreateBuffer({ bytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
				Rhi::MemoryPreference::DeviceLocal, "Array output" });
			Rhi::TextureDesc desc{};
			desc.PixelFormat = Rhi::Format::RGBA8Unorm;
			desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			images[index] = device->CreateTexture(desc);
			desc.PixelFormat = Rhi::Format::R32Uint;
			desc.Extent = { count, 1, 1 };
			desc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
			storage[index] = device->CreateTexture(desc);
			SWIM_REQUIRE(inputs[index] && parameters[index] && outputs[index] && images[index] && storage[index]);
			Rhi::TextureViewDesc view{};
			view.PixelFormat = Rhi::Format::RGBA8Unorm;
			imageViews[index] = device->CreateTextureView(*images[index], view);
			view.PixelFormat = Rhi::Format::R32Uint;
			storageViews[index] = device->CreateTextureView(*storage[index], view);
			Rhi::SamplerDesc sampler{};
			sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = Rhi::Filter::Nearest;
			samplers[index] = device->CreateSampler(sampler);
			SWIM_REQUIRE(imageViews[index] && storageViews[index] && samplers[index]);
		}
		auto imageUpload = device->CreateBuffer({ 8, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Array sampled pixels" });
		auto readback = device->CreateBuffer({ bytes * 4, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Array readback" });
		SWIM_REQUIRE(imageUpload && readback);
		const std::array<std::uint8_t, 8> pixels{ 255, 0, 0, 255, 0, 0, 0, 255 };
		imageUpload->Write(0, std::as_bytes(std::span(pixels)));
		std::array<std::unique_ptr<Rhi::DescriptorTable>, 2> tables;
		for (std::size_t set = 0; set < tables.size(); ++set)
		{
			const auto& schema = interface.DescriptorSchemas[set];
			tables[set] = device->CreateDescriptorTable({ layout.get(), schema.Space, 0, "Array table" });
			SWIM_REQUIRE(tables[set]);
			std::vector<Rhi::DescriptorWrite> writes;
			for (const auto& binding : schema.Bindings)
			{
				SWIM_REQUIRE_EQUAL(binding.Count, 2u);
				// Deliberately write element 1 before element 0.
				for (const std::uint32_t index : { 1u, 0u })
				{
					Rhi::DescriptorWrite write{};
					write.Binding = binding.Binding;
					write.ArrayIndex = index;
					switch (binding.Type)
					{
					case Rhi::DescriptorType::ReadOnlyStorageBuffer: write.BufferResource = inputs[index].get(); break;
					case Rhi::DescriptorType::UniformBuffer: write.BufferResource = parameters[index].get(); break;
					case Rhi::DescriptorType::StorageBuffer: write.BufferResource = outputs[index].get(); break;
					case Rhi::DescriptorType::SampledTexture: write.TextureResource = imageViews[index].get(); break;
					case Rhi::DescriptorType::StorageTexture: write.TextureResource = storageViews[index].get(); break;
					case Rhi::DescriptorType::Sampler: write.SamplerResource = samplers[index].get(); break;
					default: throw std::runtime_error("Unexpected reflected array descriptor");
					}
					writes.push_back(write);
				}
			}
			tables[set]->Write(writes);
		}
		// Keep this final owner alive until all referenced GPU resources can retire.
		// Each frame drains before rewriting inputs; all work stays on one family.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Compute, 2 });
		SWIM_REQUIRE(frames);
		std::array<std::array<std::uint32_t, count>, 2> source{};
		std::array<std::uint32_t, count * 4> actual{};
		const auto rw = Rhi::ResourceState::ShaderRead | Rhi::ResourceState::ShaderWrite;
		for (std::uint32_t frame = 0; frame < 4; ++frame)
		{
			for (std::uint32_t index = 0; index < 2; ++index)
			{
				for (std::uint32_t item = 0; item < count; ++item)
				{
					source[index][item] = index * 1000 + frame * 101 + item;
				}
				inputs[index]->Write(0, std::as_bytes(std::span(source[index])));
				const std::array<std::uint32_t, 4> values{ 3 + index, 7 + frame, 0, 0 };
				parameters[index]->Write(0, std::as_bytes(std::span(values)));
			}
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			if (frame == 0)
			{
				commands.Transition(*imageUpload, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
			}
			for (std::uint32_t index = 0; index < 2; ++index)
			{
				if (frame == 0)
				{
					commands.Transition(*images[index], Rhi::ResourceState::Undefined, Rhi::ResourceState::CopyDestination);
					commands.CopyBufferToTexture(*imageUpload, *images[index], { index * 4u, {}, {}, { 1, 1, 1 } });
					commands.Transition(*images[index], Rhi::ResourceState::CopyDestination, Rhi::ResourceState::ShaderRead);
				}
				commands.Transition(*inputs[index], Rhi::ResourceState::HostWrite, Rhi::ResourceState::ShaderRead | Rhi::ResourceState::CopySource);
				commands.Transition(*parameters[index], Rhi::ResourceState::HostWrite, Rhi::ResourceState::UniformBuffer);
				const auto before = frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::CopySource;
				commands.Transition(*outputs[index], before, Rhi::ResourceState::CopyDestination);
				commands.CopyBuffer(*inputs[index], *outputs[index], { 0, 0, bytes });
				commands.Transition(*outputs[index], Rhi::ResourceState::CopyDestination, rw);
				commands.Transition(*storage[index], before, Rhi::ResourceState::CopyDestination);
				commands.CopyBufferToTexture(*inputs[index], *storage[index], { 0, {}, {}, { count, 1, 1 } });
				commands.Transition(*storage[index], Rhi::ResourceState::CopyDestination, rw);
			}
			commands.BindComputePipeline(*pipeline);
			for (std::size_t set = 0; set < tables.size(); ++set)
			{
				commands.BindDescriptorTable(interface.DescriptorSchemas[set].Space, *tables[set]);
			}
			std::array<std::uint32_t, 4> push{ active, frame * 5, 0, 0 };
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(push)));
			commands.Dispatch(8, 1, 1);
			for (std::size_t index = 0; index < 2; ++index)
			{
				commands.Transition(*outputs[index], rw, rw);
				commands.Transition(*storage[index], rw, rw);
			}
			push[2] = 1;
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 8, std::as_bytes(std::span(push).subspan(2, 1)));
			commands.Dispatch(8, 1, 1);
			commands.Transition(*readback, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
			for (std::uint32_t index = 0; index < 2; ++index)
			{
				commands.Transition(*outputs[index], rw, Rhi::ResourceState::CopySource);
				commands.CopyBuffer(*outputs[index], *readback, { 0, index * bytes, bytes });
				commands.Transition(*storage[index], rw, Rhi::ResourceState::CopySource);
				commands.CopyTextureToBuffer(*storage[index], *readback, { (index + 2) * bytes, {}, {}, { count, 1, 1 } });
			}
			commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.End();
			frames->SubmitCurrent();
			frames->Drain();
			readback->Read(0, std::as_writable_bytes(std::span(actual)));
			for (std::uint32_t index = 0; index < 2; ++index)
			{
				for (std::uint32_t item = 0; item < count; ++item)
				{
					const auto value = source[index][item] * (3 + index) + 7 + frame + push[1] + (index == 0 ? 17 : 0);
					SWIM_CHECK_EQUAL(actual[index * count + item], item < active ? value + 11 : source[index][item]);
					SWIM_CHECK_EQUAL(actual[(index + 2) * count + item], item < active ? value + 114 : source[index][item]);
				}
			}
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "FixedDescriptorArraysAndReadback",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunDescriptorArraySmoke); } });
		}
		return true;
	}();

} // namespace
