#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_SAMPLED_INTEGER_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	void RunSampledIntegerSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_SAMPLED_INTEGER_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Sampled integer smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_SAMPLED_INTEGER_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		std::ifstream file(SWIM_RHI_SAMPLED_INTEGER_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Sampled integer smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Sampled integer" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Sampled integer layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "Sampled integer compute" });
		SWIM_REQUIRE(pipeline);
		constexpr std::uint32_t count = 64;
		constexpr std::uint32_t active = count - 3;
		constexpr std::uint64_t imageBytes = count * sizeof(std::uint32_t);
		constexpr std::uint64_t outputBytes = imageBytes * 4;
		const std::array formats{ Rhi::Format::R32Uint, Rhi::Format::R8Uint, Rhi::Format::R32Sint, Rhi::Format::RGBA8Unorm };
		std::array<std::unique_ptr<Rhi::Texture>, 4> textures;
		std::array<std::unique_ptr<Rhi::TextureView>, 4> views;
		for (std::size_t index = 0; index < textures.size(); ++index)
		{
			Rhi::TextureDesc desc{};
			desc.PixelFormat = formats[index];
			desc.Extent = { count * 2, 2, 1 };
			desc.MipLevels = 2;
			desc.ArrayLayers = 2;
			desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			textures[index] = device->CreateTexture(desc);
			SWIM_REQUIRE(textures[index]);
			Rhi::TextureViewDesc view{};
			view.PixelFormat = formats[index];
			view.BaseMipLevel = 1;
			view.BaseArrayLayer = 1;
			views[index] = device->CreateTextureView(*textures[index], view);
			SWIM_REQUIRE(views[index]);
		}
		auto upload = device->CreateBuffer({ outputBytes, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Sampled upload" });
		auto guards = device->CreateBuffer({ outputBytes, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Sampled guards" });
		auto output = device->CreateBuffer({ outputBytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::DeviceLocal, "Sampled output" });
		auto readback = device->CreateBuffer({ outputBytes, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Sampled readback" });
		SWIM_REQUIRE(upload && guards && output && readback);
		std::array<std::uint32_t, count * 4> expected{}, actual{};
		expected.fill(0xDEADBEEFu);
		guards->Write(0, std::as_bytes(std::span(expected)));
		std::array<std::unique_ptr<Rhi::DescriptorTable>, 2> tables;
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), tables.size());
		for (std::size_t set = 0; set < tables.size(); ++set)
		{
			const auto& schema = interface.DescriptorSchemas[set];
			tables[set] = device->CreateDescriptorTable({ layout.get(), schema.Space, 0, "Sampled table" });
			SWIM_REQUIRE(tables[set]);
			std::vector<Rhi::DescriptorWrite> writes;
			for (const auto& binding : schema.Bindings)
			{
				for (std::uint32_t remaining = binding.Count; remaining > 0; --remaining)
				{
					Rhi::DescriptorWrite write{};
					write.Binding = binding.Binding;
					write.ArrayIndex = remaining - 1;
					if (binding.Type == Rhi::DescriptorType::StorageBuffer)
					{
						write.BufferResource = output.get();
					}
					else
					{
						SWIM_REQUIRE_EQUAL(binding.Type, Rhi::DescriptorType::SampledTexture);
						const auto index = binding.SampledClass == Rhi::SampledTextureClass::Uint ? write.ArrayIndex :
							binding.SampledClass == Rhi::SampledTextureClass::Sint ? 2u : 3u;
						SWIM_REQUIRE(index < views.size());
						write.TextureResource = views[index].get();
					}
					writes.push_back(write);
				}
			}
			tables[set]->Write(writes);
		}
		// Declare the ring last so submitted work drains before resources retire.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Compute, 2 });
		SWIM_REQUIRE(frames);
		for (std::uint32_t frame = 0; frame < 4; ++frame)
		{
			std::array<std::uint32_t, count> unsignedPixels{}, signedBits{}, colors{};
			std::array<std::uint8_t, count> narrowPixels{};
			for (std::uint32_t item = 0; item < count; ++item)
			{
				unsignedPixels[item] = 0xF1234567u + frame * 101 + item;
				narrowPixels[item] = static_cast<std::uint8_t>(item + frame * 17);
				signedBits[item] = std::bit_cast<std::uint32_t>(-123456789 - static_cast<std::int32_t>(item + frame * 103));
				colors[item] = 0xFF000000u | ((item * 3 + frame * 13) & 255u);
				if (item < active)
				{
					expected[item * 4] = unsignedPixels[item] + frame;
					expected[item * 4 + 1] = narrowPixels[item];
					expected[item * 4 + 2] = signedBits[item];
					expected[item * 4 + 3] = colors[item] & 255u;
				}
			}
			upload->Write(0, std::as_bytes(std::span(unsignedPixels)));
			upload->Write(imageBytes, std::as_bytes(std::span(narrowPixels)));
			upload->Write(imageBytes * 2, std::as_bytes(std::span(signedBits)));
			upload->Write(imageBytes * 3, std::as_bytes(std::span(colors)));
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			commands.Transition(*upload, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
			if (frame == 0)
			{
				commands.Transition(*guards, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
			}
			commands.Transition(*output, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::CopySource, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*guards, *output, { 0, 0, outputBytes });
			commands.Transition(*output, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::ShaderWrite);
			for (std::uint32_t index = 0; index < textures.size(); ++index)
			{
				const Rhi::TextureSubresourceRange range{ 1, 1, 1, 1 };
				commands.Transition(*textures[index], frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::ShaderRead,
					Rhi::ResourceState::CopyDestination, range);
				commands.CopyBufferToTexture(*upload, *textures[index], { imageBytes * index, { 1, 1 }, {}, { count, 1, 1 } });
				commands.Transition(*textures[index], Rhi::ResourceState::CopyDestination, Rhi::ResourceState::ShaderRead, range);
			}
			commands.BindComputePipeline(*pipeline);
			for (std::size_t set = 0; set < tables.size(); ++set)
			{
				commands.BindDescriptorTable(interface.DescriptorSchemas[set].Space, *tables[set]);
			}
			const std::array<std::uint32_t, 4> push{ active, frame, 0, 0 };
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(push)));
			commands.Dispatch(count / 8, 1, 1);
			commands.Transition(*output, Rhi::ResourceState::ShaderWrite, Rhi::ResourceState::CopySource);
			commands.Transition(*readback, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*output, *readback, { 0, 0, outputBytes });
			commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.End();
			frames->SubmitCurrent();
			frames->Drain();
			readback->Read(0, std::as_writable_bytes(std::span(actual)));
			for (std::size_t item = 0; item < actual.size(); ++item)
			{
				SWIM_CHECK_EQUAL(actual[item], expected[item]);
			}
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "SampledIntegerTexturesAndReadback",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunSampledIntegerSmoke); } });
		}
		return true;
	}();

} // namespace
