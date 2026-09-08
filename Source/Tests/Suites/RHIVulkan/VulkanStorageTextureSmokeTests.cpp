#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_STORAGE_TEXTURE_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	void RunStorageTextureSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_STORAGE_TEXTURE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Storage texture smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_STORAGE_TEXTURE_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas.size(), 1u);
		SWIM_REQUIRE_EQUAL(interface.DescriptorSchemas[0].Bindings.size(), 3u);
		SWIM_REQUIRE((interface.ComputeThreadGroupSize == std::array<std::uint32_t, 3>{ 8, 8, 1 }));
		SWIM_REQUIRE_EQUAL(interface.PushConstants.size(), 1u);
		SWIM_REQUIRE_EQUAL(interface.PushConstants[0].Size, 24u);
		std::ifstream file(SWIM_RHI_STORAGE_TEXTURE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Storage texture smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Storage texture smoke" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Storage texture layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "Storage texture pipeline" });
		SWIM_REQUIRE(pipeline);
		constexpr std::uint32_t width = 13;
		constexpr std::uint32_t height = 9;
		constexpr std::uint32_t pixels = width * height;
		constexpr std::array<Rhi::Format, 3> formats{ Rhi::Format::RGBA32Float, Rhi::Format::R32Uint, Rhi::Format::R32Sint };
		constexpr std::array<std::uint64_t, 3> offsets{ 0, pixels * 16, pixels * 20 };
		constexpr std::size_t bytes = pixels * 24;
		std::array<std::unique_ptr<Rhi::Texture>, 3> textures;
		std::array<std::unique_ptr<Rhi::TextureView>, 3> views;
		std::array<Rhi::DescriptorWrite, 3> writes{};
		for (std::size_t index = 0; index < textures.size(); ++index)
		{
			Rhi::TextureDesc desc;
			desc.Extent = { width * 2, height * 2, 1 };
			desc.PixelFormat = formats[index];
			desc.Usage = Rhi::TextureUsage::Storage | Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
			desc.MipLevels = 2;
			desc.ArrayLayers = 2;
			desc.DebugName = "Storage smoke image";
			textures[index] = device->CreateTexture(desc);
			SWIM_REQUIRE(textures[index]);
			// Bind one nonzero mip and array layer as an ordinary 2D image.
			views[index] = device->CreateTextureView(*textures[index],
				{ Rhi::TextureViewDimension::Texture2D, formats[index], 1, 1, 1, 1, "Storage smoke view" });
			SWIM_REQUIRE(views[index]);
			bool found = false;
			for (const auto& binding : interface.DescriptorSchemas[0].Bindings)
			{
				if (binding.StorageTextureFormat == formats[index])
				{
					writes[index].Binding = binding.Binding;
					found = true;
				}
			}
			SWIM_REQUIRE(found);
			writes[index].TextureResource = views[index].get();
		}
		auto table = device->CreateDescriptorTable({ layout.get(), interface.DescriptorSchemas[0].Space });
		SWIM_REQUIRE(table);
		table->Write(writes);
		auto upload = device->CreateBuffer({ bytes, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Storage smoke upload" });
		auto readback = device->CreateBuffer({ bytes, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Storage smoke readback" });
		SWIM_REQUIRE(upload && readback);
		std::array<std::byte, bytes> initial{};
		std::array<std::byte, bytes> actual{};
		const std::array<float, 4> guardFloat{ 31, 32, 33, 34 };
		const std::uint32_t guardUint = 83;
		const std::int32_t guardInt = -79;
		for (std::size_t pixel = 0; pixel < pixels; ++pixel)
		{
			std::memcpy(initial.data() + offsets[0] + pixel * 16, guardFloat.data(), 16);
			std::memcpy(initial.data() + offsets[1] + pixel * 4, &guardUint, 4);
			std::memcpy(initial.data() + offsets[2] + pixel * 4, &guardInt, 4);
		}
		// All image work stays on this family. Last owner drains before resources
		// are destroyed on failure; each frame drains before host upload reuse.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Compute, 2 });
		SWIM_REQUIRE(frames);
		const Rhi::TextureSubresourceRange range{ 1, 1, 1, 1 };
		const auto rw = Rhi::ResourceState::ShaderRead | Rhi::ResourceState::ShaderWrite;
		for (std::uint32_t frame = 0; frame < 4; ++frame)
		{
			upload->Write(0, initial);
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			commands.BeginDebugLabel("Typed storage images: upload, dependent passes, readback");
			commands.Transition(*upload, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
			for (std::size_t index = 0; index < textures.size(); ++index)
			{
				commands.Transition(*textures[index], frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::CopySource,
					Rhi::ResourceState::CopyDestination, range);
				commands.CopyBufferToTexture(*upload, *textures[index], { offsets[index], { 1, 1 }, {}, { width, height, 1 } });
				commands.Transition(*textures[index], Rhi::ResourceState::CopyDestination, rw, range);
			}
			commands.BindComputePipeline(*pipeline);
			commands.BindDescriptorTable(interface.DescriptorSchemas[0].Space, *table);
			std::array<std::uint32_t, 6> constants{ width, height, width - 2, height - 2, frame * 23, 0 };
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(constants)));
			commands.Dispatch(2, 2, 1);
			for (auto& texture : textures)
			{
				commands.Transition(*texture, rw, rw, range);
			}
			constants[5] = 1;
			commands.PushConstants(Rhi::ShaderStageMask::Compute, 20, std::as_bytes(std::span(constants).subspan(5)));
			commands.Dispatch(2, 2, 1);
			commands.Transition(*readback, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
			for (std::size_t index = 0; index < textures.size(); ++index)
			{
				commands.Transition(*textures[index], rw, Rhi::ResourceState::CopySource, range);
				commands.CopyTextureToBuffer(*textures[index], *readback, { offsets[index], { 1, 1 }, {}, { width, height, 1 } });
			}
			commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.EndDebugLabel();
			commands.End();
			frames->SubmitCurrent();
			frames->Drain();
			readback->Read(0, actual);
			for (std::uint32_t pixel = 0; pixel < pixels; ++pixel)
			{
				const auto x = pixel % width;
				const auto y = pixel / width;
				const auto value = frame * 23 + x + 17 * y;
				const bool active = x < width - 2 && y < height - 2;
				const std::array<float, 4> expected = active ?
					std::array<float, 4>{ float(value + 2), float(value + 5), float(value + 10), 1 } : guardFloat;
				std::array<float, 4> floats{};
				std::uint32_t uintValue = 0;
				std::int32_t intValue = 0;
				std::memcpy(floats.data(), actual.data() + offsets[0] + pixel * 16, 16);
				std::memcpy(&uintValue, actual.data() + offsets[1] + pixel * 4, 4);
				std::memcpy(&intValue, actual.data() + offsets[2] + pixel * 4, 4);
				SWIM_CHECK(floats == expected);
				SWIM_CHECK_EQUAL(uintValue, active ? value * 3 + 7 : guardUint);
				SWIM_CHECK_EQUAL(intValue, active ? -static_cast<std::int32_t>(value) - 7 : guardInt);
			}
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "TypedStorageTexturesAndSubresourceReadback",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunStorageTextureSmoke); } });
		}
		return true;
	}();

} // namespace
