#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/SampledDimensionData.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_SAMPLED_DIMENSIONS_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <fstream>

namespace
{

	void RunSampledDimensionSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_SAMPLED_DIMENSIONS_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Sampled dimension smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Sampled dimension smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto& adapter = graphics->GetAdapter(0);
		const bool cubes = adapter.GetInfo().Capabilities.SampledCubeArray;
		std::cerr << "[RHI sampled dimensions] cube-array coverage=" << (cubes ? "enabled" : "unsupported; base shapes still required") << '\n';
		auto device = adapter.CreateDevice();
		SWIM_REQUIRE(device);
		const auto parsed = ShaderCompiler::LoadSlangReflectionJson(cubes ?
			SWIM_RHI_SAMPLED_CUBE_ARRAY_REFLECTION_PATH : SWIM_RHI_SAMPLED_DIMENSIONS_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		std::ifstream file(cubes ? SWIM_RHI_SAMPLED_CUBE_ARRAY_SPIRV_PATH : SWIM_RHI_SAMPLED_DIMENSIONS_SPIRV_PATH,
			std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Sampled dimensions" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Sampled dimension layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "Sampled dimension compute" });
		SWIM_REQUIRE(pipeline);
		const std::uint32_t imageCount = cubes ? 7 : 6;
		std::array<std::unique_ptr<Rhi::Texture>, 7> textures;
		std::array<std::unique_ptr<Rhi::TextureView>, 7> views;
		for (std::uint32_t index = 0; index < imageCount; ++index)
		{
			const auto data = Testing::MakeSampledDimensionData(index);
			textures[index] = device->CreateTexture(data.Texture);
			SWIM_REQUIRE(textures[index]);
			views[index] = device->CreateTextureView(*textures[index], data.View);
			SWIM_REQUIRE(views[index]);
		}
		Rhi::SamplerDesc samplerDesc{};
		samplerDesc.MinFilter = samplerDesc.MagFilter = samplerDesc.MipFilter = Rhi::Filter::Nearest;
		auto sampler = device->CreateSampler(samplerDesc);
		SWIM_REQUIRE(sampler);
		constexpr std::uint64_t imageBytes = 1024;
		constexpr std::uint64_t outputBytes = 16 * 8 * sizeof(std::uint32_t);
		auto upload = device->CreateBuffer({ imageBytes * imageCount, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Dimension upload" });
		auto guards = device->CreateBuffer({ outputBytes, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Dimension guards" });
		auto output = device->CreateBuffer({ outputBytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::DeviceLocal, "Dimension output" });
		auto readback = device->CreateBuffer({ outputBytes, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Dimension readback" });
		SWIM_REQUIRE(upload && guards && output && readback);
		std::array<std::uint32_t, 128> expected{}, actual{};
		expected.fill(0xDEADBEEFu);
		guards->Write(0, std::as_bytes(std::span(expected)));
		auto table = device->CreateDescriptorTable({ layout.get(), 0, 0, "Dimension table" });
		SWIM_REQUIRE(table);
		std::vector<Rhi::DescriptorWrite> writes;
		for (std::uint32_t index = 0; index < imageCount; ++index)
		{
			Rhi::DescriptorWrite write{};
			write.Binding = index;
			write.TextureResource = views[index].get();
			writes.push_back(write);
		}
		Rhi::DescriptorWrite samplerWrite{};
		samplerWrite.Binding = 7;
		samplerWrite.SamplerResource = sampler.get();
		writes.push_back(samplerWrite);
		Rhi::DescriptorWrite outputWrite{};
		outputWrite.Binding = 8;
		outputWrite.BufferResource = output.get();
		writes.push_back(outputWrite);
		table->Write(writes);
		// The ring drains before any referenced resource is destroyed.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Compute, 2 });
		SWIM_REQUIRE(frames);
		for (std::uint32_t frame = 0; frame < 4; ++frame)
		{
			for (std::uint32_t index = 0; index < imageCount; ++index)
			{
				const auto pixels = Testing::SampledDimensionPixels(frame, index);
				upload->Write(imageBytes * index, std::as_bytes(std::span(pixels)));
			}
			for (std::uint32_t item = 0; item < 12; ++item)
			{
				for (std::uint32_t index = 0; index < imageCount; ++index)
				{
					const auto layer = index == 1 || index == 2 ? item % 2 : index == 4 ? item % 6 : index == 6 ? item : 0;
					expected[item * 8 + index] = Testing::SampledDimensionValue(frame, index, layer,
						item % 4, index < 2 ? 0 : item / 4, index == 3 ? item % 3 : 0);
				}
			}
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
			for (std::uint32_t index = 0; index < imageCount; ++index)
			{
				const auto data = Testing::MakeSampledDimensionData(index);
				const Rhi::TextureSubresourceRange range{ 1, 1, data.View.BaseArrayLayer, data.View.ArrayLayerCount };
				commands.Transition(*textures[index], frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::ShaderRead,
					Rhi::ResourceState::CopyDestination, range);
				const auto layerBytes = data.CopyExtent.Width * data.CopyExtent.Height * data.CopyExtent.Depth * sizeof(std::uint32_t);
				for (std::uint32_t layer = 0; layer < data.View.ArrayLayerCount; ++layer)
				{
					commands.CopyBufferToTexture(*upload, *textures[index], { imageBytes * index + layerBytes * layer,
						{ 1, data.View.BaseArrayLayer + layer }, {}, data.CopyExtent });
				}
				commands.Transition(*textures[index], Rhi::ResourceState::CopyDestination, Rhi::ResourceState::ShaderRead, range);
			}
			commands.BindComputePipeline(*pipeline);
			commands.BindDescriptorTable(0, *table);
			commands.Dispatch(4, 1, 1);
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
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "SampledDimensionsAndReadback",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunSampledDimensionSmoke); } });
		}
		return true;
	}();

} // namespace
