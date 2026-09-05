#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	void RunTriangleSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_TRIANGLE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Triangle smoke requires SWIM_BUILD_SHADER_COMPILER=ON and the generated Slang artifact");
#else
		using namespace Swim;
		std::ifstream file(SWIM_RHI_TRIANGLE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE_MESSAGE(file, "Compiled RHI triangle shader is missing");
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		Platform::PlatformSystem platform;
		SWIM_REQUIRE(platform.Initialize());
		Platform::WindowDesc windowDesc{};
		windowDesc.Title = "Swim RHI triangle smoke";
		windowDesc.GraphicsSupport = Platform::WindowGraphicsSupport::Vulkan;
		auto window = platform.GetWindowSystem().Create(windowDesc);
		SWIM_REQUIRE(window);
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE_MESSAGE(graphics, "Triangle smoke requires the full Swim Vulkan 1.3 baseline");
		SWIM_REQUIRE_MESSAGE(graphics->IsValidationEnabled(), "Smoke requires active Vulkan validation");
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const std::array<Rhi::ShaderStageArtifact, 2> stages{{
			{ Rhi::ShaderStageMask::Vertex, "vertexMain", bytecode },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytecode }
		}};
		auto program = device->CreateShaderProgram({ stages, {}, "RHI triangle" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "RHI triangle layout" });
		SWIM_REQUIRE(layout);
		const Rhi::Format format = Rhi::Format::RGBA8Unorm;
		Rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.Program = program.get();
		pipelineDesc.Layout = layout.get();
		pipelineDesc.ColorFormats = { &format, 1 };
		pipelineDesc.DepthStencil.DepthTest = pipelineDesc.DepthStencil.DepthWrite = false;
		pipelineDesc.Raster.Cull = Rhi::CullMode::None;
		auto pipeline = device->CreateGraphicsPipeline(pipelineDesc);
		SWIM_REQUIRE(pipeline);
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
		// Last owner drains GPU work before any referenced resource is destroyed.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2 });
		SWIM_REQUIRE(frames);
		std::vector<std::byte> directPixels(64 * 64 * 4);
		std::vector<std::byte> indexedPixels(directPixels.size());
		for (std::uint32_t pass = 0; pass < 2; ++pass)
		{
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			commands.BeginDebugLabel("RunTriangleSmoke: commands", { 0.2f, 0.6f, 0.9f, 1.0f });
			commands.Transition(*target, Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment);
			if (pass == 1)
			{
				commands.Transition(*indices, Rhi::ResourceState::HostWrite, Rhi::ResourceState::IndexBuffer);
			}
			Rhi::RenderingAttachmentDesc attachment{};
			attachment.View = view.get();
			attachment.Load = Rhi::LoadOp::Clear;
			attachment.Clear.Value = { 0, 0, 0, 1 };
			commands.BeginRendering({ { &attachment, 1 }, nullptr, { 64, 64 } });
			commands.BindGraphicsPipeline(*pipeline);
			commands.SetViewport({ 0, 0, 64, 64 });
			commands.SetScissor({ 0, 0, 64, 64 });
			if (pass == 0)
			{
				commands.Draw(3);
			}
			else
			{
				commands.BindIndexBuffer(*indices, 0, Rhi::IndexType::Uint16);
				commands.DrawIndexed(3);
			}
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
			readback->Read(0, pass == 0 ? directPixels : indexedPixels);
		}
		const auto checkPixel = [&](std::uint32_t x, std::uint32_t y, std::byte green)
		{
			const std::size_t offset = (y * 64 + x) * 4;
			SWIM_CHECK(directPixels[offset] == std::byte{ 0 } && directPixels[offset + 1] == green &&
				directPixels[offset + 2] == std::byte{ 0 } && directPixels[offset + 3] == std::byte{ 255 });
		};
		checkPixel(32, 32, std::byte{ 255 });
		checkPixel(2, 2, std::byte{ 0 });
		checkPixel(12, 50, std::byte{ 255 });
		checkPixel(12, 14, std::byte{ 0 });
		SWIM_CHECK(directPixels == indexedPixels);
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "TrianglePixelsAndIndexedParity", SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunTriangleSmoke); } });
		}
		return true;
	}();

} // namespace
