#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"

#ifdef SWIM_RHI_PUSH_CONSTANTS_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	void RunPushConstantSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_PUSH_CONSTANTS_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Push-constant smoke requires SWIM_BUILD_SHADER_COMPILER=ON and generated Slang artifacts");
#else
		using namespace Swim;
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_PUSH_CONSTANTS_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto interface = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(interface, interface.Error);
		SWIM_REQUIRE_EQUAL(interface.Interface.PushConstants.size(), 1u);
		const auto range = interface.Interface.PushConstants[0];
		struct DrawConstants
		{
			std::array<float, 2> Offset;
			std::array<float, 2> Scale;
			std::array<float, 4> Color;
		};
		static_assert(sizeof(DrawConstants) == 32 && offsetof(DrawConstants, Color) == 16);
		SWIM_REQUIRE_EQUAL(range.Offset, 0u);
		SWIM_REQUIRE_EQUAL(range.Size, sizeof(DrawConstants));
		std::ifstream file(SWIM_RHI_PUSH_CONSTANTS_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE_MESSAGE(file, "Compiled RHI push-constant shader is missing");
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		Platform::PlatformSystem platform;
		SWIM_REQUIRE(platform.Initialize());
		Platform::WindowDesc windowDesc{};
		windowDesc.Title = "Swim RHI push-constant smoke";
		windowDesc.GraphicsSupport = Platform::WindowGraphicsSupport::Vulkan;
		auto window = platform.GetWindowSystem().Create(windowDesc);
		SWIM_REQUIRE(window);
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE_MESSAGE(graphics, "Push-constant smoke requires the full Swim Vulkan baseline");
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const std::array<Rhi::ShaderStageArtifact, 2> stages{{
			{ Rhi::ShaderStageMask::Vertex, "vertexMain", bytecode },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytecode }
		}};
		auto program = device->CreateShaderProgram({ stages,
			{ interface.Interface.DescriptorSchemas, interface.Interface.PushConstants }, "RHI push constants" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "RHI push layout" });
		auto compatibleLayout = device->CreatePipelineLayout({ program.get(), "RHI compatible push layout" });
		SWIM_REQUIRE(layout && compatibleLayout);
		const Rhi::Format format = Rhi::Format::RGBA8Unorm;
		Rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.Program = program.get();
		pipelineDesc.Layout = layout.get();
		pipelineDesc.ColorFormats = { &format, 1 };
		pipelineDesc.DepthStencil.DepthTest = pipelineDesc.DepthStencil.DepthWrite = false;
		pipelineDesc.Raster.Cull = Rhi::CullMode::None;
		auto pipeline = device->CreateGraphicsPipeline(pipelineDesc);
		pipelineDesc.Layout = compatibleLayout.get();
		auto compatiblePipeline = device->CreateGraphicsPipeline(pipelineDesc);
		SWIM_REQUIRE(pipeline && compatiblePipeline);
		Rhi::TextureDesc targetDesc{};
		targetDesc.Extent = { 64, 64, 1 };
		targetDesc.PixelFormat = format;
		targetDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource;
		auto target = device->CreateTexture(targetDesc);
		SWIM_REQUIRE(target);
		auto view = device->CreateTextureView(*target, {});
		SWIM_REQUIRE(view);
		auto readback = device->CreateBuffer({ 64 * 64 * 4, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, {} });
		auto indices = device->CreateBuffer({ 6, Rhi::BufferUsage::Index, Rhi::MemoryPreference::CpuToGpu, {} });
		SWIM_REQUIRE(readback && indices);
		const std::array<std::uint16_t, 3> indexData{ 0, 1, 2 };
		indices->Write(0, std::as_bytes(std::span(indexData)));
		// Last resource owner drains before referenced resources are destroyed.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2 });
		SWIM_REQUIRE(frames);
		std::vector<std::byte> directPixels(64 * 64 * 4);
		std::vector<std::byte> indexedPixels(directPixels.size());
		for (std::uint32_t pass = 0; pass < 4; ++pass)
		{
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			commands.BeginDebugLabel("Push constants: partial updates and compatible pipelines", { 0.2f, 0.6f, 0.9f, 1.0f });
			commands.Transition(*target, pass == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::CopySource,
				Rhi::ResourceState::ColorAttachment);
			if (pass == 0)
			{
				commands.Transition(*indices, Rhi::ResourceState::HostWrite, Rhi::ResourceState::IndexBuffer);
			}
			commands.BindGraphicsPipeline(*pipeline);
			DrawConstants values{ { -0.5f, 0 }, { 0.3f, 0.5f }, pass < 2 ?
				std::array<float, 4>{ 1, 0, 0, 1 } : std::array<float, 4>{ 0, 0, 1, 1 } };
			commands.PushConstants(range.Stages, 0, std::as_bytes(std::span(&values, 1)));
			Rhi::RenderingAttachmentDesc attachment{};
			attachment.View = view.get();
			attachment.Load = Rhi::LoadOp::Clear;
			attachment.Clear.Value = { 0, 0, 0, 1 };
			commands.BeginRendering({ { &attachment, 1 }, nullptr, { 64, 64 } });
			commands.SetViewport({ 0, 0, 64, 64 });
			commands.SetScissor({ 0, 0, 64, 64 });
			if (pass % 2 != 0)
			{
				commands.BindIndexBuffer(*indices, 0, Rhi::IndexType::Uint16);
			}
			const auto draw = [&]
			{
				if (pass % 2 == 0)
				{
					commands.Draw(3);
				}
				else
				{
					commands.DrawIndexed(3);
				}
			};
			draw();
			commands.BindGraphicsPipeline(*compatiblePipeline);
			values.Offset = { 0.5f, 0 };
			values.Color = pass < 2 ? std::array<float, 4>{ 0, 1, 0, 1 } : std::array<float, 4>{ 1, 1, 0, 1 };
			// Scale remains from the first write through a distinct compatible layout.
			commands.PushConstants(range.Stages, offsetof(DrawConstants, Offset), std::as_bytes(std::span(values.Offset)));
			commands.PushConstants(range.Stages, offsetof(DrawConstants, Color), std::as_bytes(std::span(values.Color)));
			draw();
			commands.EndRendering();
			commands.Transition(*target, Rhi::ResourceState::ColorAttachment, Rhi::ResourceState::CopySource);
			commands.Transition(*readback, pass == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
			Rhi::BufferTextureCopyRegion copy{};
			copy.Extent = targetDesc.Extent;
			commands.CopyTextureToBuffer(*target, *readback, copy);
			commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.EndDebugLabel();
			commands.End();
			frames->SubmitCurrent();
			frames->Drain();
			auto& pixels = pass % 2 == 0 ? directPixels : indexedPixels;
			readback->Read(0, pixels);
			const auto checkPixel = [&](std::uint32_t x, std::uint32_t y, std::array<std::byte, 4> expected)
			{
				const std::size_t offset = (y * 64 + x) * 4;
				for (std::size_t channel = 0; channel < expected.size(); ++channel)
				{
					SWIM_CHECK_EQUAL(pixels[offset + channel], expected[channel]);
				}
			};
			const auto on = std::byte{ 255 };
			const auto off = std::byte{ 0 };
			checkPixel(16, 32, pass < 2 ? std::array{ on, off, off, on } : std::array{ off, off, on, on });
			checkPixel(48, 32, pass < 2 ? std::array{ off, on, off, on } : std::array{ on, on, off, on });
			checkPixel(32, 32, { off, off, off, on });
			checkPixel(2, 2, { off, off, off, on });
			if (pass % 2 != 0)
			{
				SWIM_CHECK(directPixels == indexedPixels);
			}
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "PushConstantUpdatesAndCompatiblePipelinePixels", SWIM_TEST_LOCATION,
				+[] { Swim::Testing::RunValidatedVulkanSmoke(&RunPushConstantSmoke); } });
		}
		return true;
	}();

} // namespace
