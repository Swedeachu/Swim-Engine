#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_DEPTH_SAMPLING_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <cmath>
#include <fstream>

namespace
{
	void RunDepthSamplingSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_DEPTH_SAMPLING_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Depth sampling smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Depth sampling smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const auto parsed = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_DEPTH_SAMPLING_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(parsed, parsed.Error);
		const auto converted = ShaderCompiler::BuildRhiShaderInterface(parsed.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		const auto& interface = converted.Interface;
		std::ifstream file(SWIM_RHI_DEPTH_SAMPLING_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytecode };
		auto program = device->CreateShaderProgram({ { &stage, 1 },
			{ interface.DescriptorSchemas, interface.PushConstants, interface.ComputeThreadGroupSize }, "Depth sampling" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Depth sampling layout" });
		SWIM_REQUIRE(layout);
		auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, "Depth sampling compute" });
		SWIM_REQUIRE(pipeline);
		std::array<std::unique_ptr<Rhi::Texture>, 2> images;
		std::array<std::unique_ptr<Rhi::TextureView>, 2> attachments, views;
		for (std::size_t index = 0; index < images.size(); ++index)
		{
			Rhi::TextureDesc desc{};
			desc.PixelFormat = index == 0 ? Rhi::Format::D32Float : Rhi::Format::D24UnormS8Uint;
			desc.Usage = Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled;
			desc.Extent = { 8, 8, 1 };
			desc.MipLevels = desc.ArrayLayers = 2;
			images[index] = device->CreateTexture(desc);
			if (!images[index] && index == 1)
			{
				desc.PixelFormat = Rhi::Format::D32FloatS8Uint;
				images[index] = device->CreateTexture(desc);
			}
			SWIM_REQUIRE_MESSAGE(images[index], "Depth smoke requires sampled D32 and a supported sampled depth/stencil format");
			Rhi::TextureViewDesc view{};
			view.BaseMipLevel = view.BaseArrayLayer = 1;
			attachments[index] = device->CreateTextureView(*images[index], view);
			view.Aspect = Rhi::TextureAspect::Depth;
			views[index] = device->CreateTextureView(*images[index], view);
			SWIM_REQUIRE(attachments[index] && views[index]);
		}
		std::array<std::unique_ptr<Rhi::Sampler>, 3> samplers;
		for (std::size_t index = 0; index < samplers.size(); ++index)
		{
			Rhi::SamplerDesc desc{};
			desc.MinFilter = desc.MagFilter = desc.MipFilter = Rhi::Filter::Nearest;
			desc.EnableComparison = index < 2;
			desc.Comparison = index == 0 ? Rhi::CompareOp::LessEqual : Rhi::CompareOp::Greater;
			samplers[index] = device->CreateSampler(desc);
			SWIM_REQUIRE(samplers[index]);
		}
		constexpr std::uint64_t bytes = 64 * sizeof(float);
		auto guards = device->CreateBuffer({ bytes, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, "Depth guards" });
		auto output = device->CreateBuffer({ bytes, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination,
			Rhi::MemoryPreference::DeviceLocal, "Depth output" });
		auto readback = device->CreateBuffer({ bytes, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Depth readback" });
		SWIM_REQUIRE(guards && output && readback);
		std::array<float, 64> expected{}, actual{};
		expected.fill(-7.0f);
		guards->Write(0, std::as_bytes(std::span(expected)));
		auto table = device->CreateDescriptorTable({ layout.get(), 0, 0, "Depth table" });
		SWIM_REQUIRE(table);
		std::array<Rhi::DescriptorWrite, 6> writes{};
		writes[0].TextureResource = views[0].get();
		writes[1].Binding = 1;
		writes[1].TextureResource = views[1].get();
		for (std::size_t index = 0; index < samplers.size(); ++index)
		{
			writes[index + 2].Binding = index < 2 ? 2 : 3;
			writes[index + 2].ArrayIndex = index == 1 ? 1 : 0;
			writes[index + 2].SamplerResource = samplers[index].get();
		}
		writes[5].Binding = 4;
		writes[5].BufferResource = output.get();
		table->Write(writes);
		// Rendering and compute share one graphics family; drain before host reuse.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2 });
		SWIM_REQUIRE(frames);
		const std::array clearValues{ 0.25f, 0.75f, 0.5f };
		for (std::uint32_t frame = 0; frame < clearValues.size(); ++frame)
		{
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			if (frame == 0)
			{
				commands.Transition(*guards, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
			}
			commands.Transition(*output, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::CopySource, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*guards, *output, { 0, 0, bytes });
			commands.Transition(*output, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::ShaderWrite);
			for (std::size_t index = 0; index < images.size(); ++index)
			{
				const auto value = index == 0 ? clearValues[frame] : 1.0f - clearValues[frame];
				commands.Transition(*images[index], frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::ShaderRead,
					Rhi::ResourceState::DepthStencilWrite, { 1, 1, 1, 1 });
				Rhi::DepthStencilAttachmentDesc depth{ attachments[index].get(), Rhi::LoadOp::Clear, Rhi::StoreOp::Store, value, 23 };
				commands.BeginRendering({ {}, &depth, { 4, 4 } });
				commands.EndRendering();
				commands.Transition(*images[index], Rhi::ResourceState::DepthStencilWrite, Rhi::ResourceState::ShaderRead, { 1, 1, 1, 1 });
				for (std::uint32_t item = 0; item < 6; ++item)
				{
					const auto offset = (item * 2 + index) * 4;
					expected[offset] = expected[offset + 1] = value;
					expected[offset + 2] = static_cast<float>(item) * 0.2f <= value ? 1.0f : 0.0f;
					expected[offset + 3] = static_cast<float>(item) * 0.2f > value ? 1.0f : 0.0f;
				}
			}
			commands.BindComputePipeline(*pipeline);
			commands.BindDescriptorTable(0, *table);
			commands.Dispatch(2, 1, 1);
			commands.Transition(*output, Rhi::ResourceState::ShaderWrite, Rhi::ResourceState::CopySource);
			commands.Transition(*readback, frame == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
			commands.CopyBuffer(*output, *readback, { 0, 0, bytes });
			commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.End();
			frames->SubmitCurrent();
			frames->Drain();
			readback->Read(0, std::as_writable_bytes(std::span(actual)));
			for (std::size_t item = 0; item < actual.size(); ++item)
			{
				SWIM_CHECK(std::abs(actual[item] - expected[item]) < 0.000002f);
			}
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "DepthSamplingAndComparisonReadback",
				SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunDepthSamplingSmoke); } });
		}
		return true;
	}();

} // namespace
