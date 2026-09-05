#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Framework/Test.h"

#ifdef SWIM_RHI_TEXTURE_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif

#include <array>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

	void RunTextureSmoke()
	{
#ifndef SWIM_RHI_TEXTURE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "Texture smoke requires SWIM_BUILD_SHADER_COMPILER=ON and generated shader/reflection artifacts");
#else
		using namespace Swim;
		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_TEXTURE_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto interface = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(interface, interface.Error);
		SWIM_REQUIRE_EQUAL(interface.Interface.DescriptorSchemas.size(), 1u);
		const auto& schema = interface.Interface.DescriptorSchemas.front();
		const auto findBinding = [&](Rhi::DescriptorType type)
		{
			for (const auto& binding : schema.Bindings)
			{
				if (binding.Type == type)
				{
					return binding.Binding;
				}
			}
			throw std::runtime_error("Texture smoke reflection is missing a required resource");
		};
		const auto textureBinding = findBinding(Rhi::DescriptorType::SampledTexture);
		const auto samplerBinding = findBinding(Rhi::DescriptorType::Sampler);
		std::ifstream file(SWIM_RHI_TEXTURE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytecode(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
		SWIM_REQUIRE(file);
		Platform::PlatformSystem platform;
		SWIM_REQUIRE(platform.Initialize());
		Platform::WindowDesc windowDesc{};
		windowDesc.Title = "Swim RHI reflected texture smoke";
		windowDesc.GraphicsSupport = Platform::WindowGraphicsSupport::Vulkan;
		auto window = platform.GetWindowSystem().Create(windowDesc);
		SWIM_REQUIRE(window);
		auto graphics = RhiVulkan::CreateGraphicsSystem();
		SWIM_REQUIRE(graphics);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const std::array<Rhi::ShaderStageArtifact, 2> stages{{
			{ Rhi::ShaderStageMask::Vertex, "vertexMain", bytecode },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytecode }
		}};
		auto program = device->CreateShaderProgram({ stages,
			{ interface.Interface.DescriptorSchemas, interface.Interface.PushConstants }, "RHI texture smoke" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), {} });
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
		Rhi::SamplerDesc samplerDesc{};
		samplerDesc.MinFilter = samplerDesc.MagFilter = samplerDesc.MipFilter = Rhi::Filter::Nearest;
		samplerDesc.AddressU = samplerDesc.AddressV = Rhi::SamplerAddressMode::ClampToEdge;
		auto sampler = device->CreateSampler(samplerDesc);
		SWIM_REQUIRE(sampler);
		const std::array<std::uint8_t, 16> texels{ 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
		auto upload = device->CreateBuffer({ 32, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, {} });
		SWIM_REQUIRE(upload);
		std::array<std::uint8_t, 32> patterns{};
		for (std::size_t index = 0; index < 16; ++index)
		{
			patterns[index] = texels[index];
			patterns[index + 16] = texels[(index + 8) % 16];
		}
		upload->Write(0, std::as_bytes(std::span(patterns)));
		std::array<std::unique_ptr<Rhi::Texture>, 2> textures;
		std::array<std::unique_ptr<Rhi::TextureView>, 2> views;
		std::array<std::unique_ptr<Rhi::DescriptorTable>, 2> tables;
		Rhi::TextureDesc textureDesc{};
		textureDesc.PixelFormat = format;
		textureDesc.Extent = { 2, 2, 1 };
		textureDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
		for (std::size_t index = 0; index < textures.size(); ++index)
		{
			textures[index] = device->CreateTexture(textureDesc);
			SWIM_REQUIRE(textures[index]);
			views[index] = device->CreateTextureView(*textures[index], {});
			tables[index] = device->CreateDescriptorTable({ layout.get(), schema.Space, 0, {} });
			SWIM_REQUIRE(views[index] && tables[index]);
			std::array<Rhi::DescriptorWrite, 2> writes{};
			writes[0].Binding = textureBinding;
			writes[0].TextureResource = views[index].get();
			writes[1].Binding = samplerBinding;
			writes[1].SamplerResource = sampler.get();
			tables[index]->Write(writes);
		}
		Rhi::TextureDesc targetDesc{};
		targetDesc.PixelFormat = format;
		targetDesc.Extent = { 16, 16, 1 };
		targetDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource;
		auto target = device->CreateTexture(targetDesc);
		SWIM_REQUIRE(target);
		auto targetView = device->CreateTextureView(*target, {});
		auto readback = device->CreateBuffer({ 16 * 16 * 4, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, {} });
		SWIM_REQUIRE(targetView && readback);
		// Drain before any referenced tables, views, textures or sampler unwind.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2 });
		SWIM_REQUIRE(frames);
		for (std::uint32_t pass = 0; pass < 2; ++pass)
		{
			frames->BeginFrame();
			auto& commands = frames->CreateCommandList();
			commands.Begin();
			if (pass == 0)
			{
				commands.Transition(*upload, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
			}
			commands.Transition(*textures[pass], Rhi::ResourceState::Undefined, Rhi::ResourceState::CopyDestination);
			Rhi::BufferTextureCopyRegion copy{};
			copy.BufferOffset = pass * 16;
			copy.Extent = textureDesc.Extent;
			commands.CopyBufferToTexture(*upload, *textures[pass], copy);
			commands.Transition(*textures[pass], Rhi::ResourceState::CopyDestination, Rhi::ResourceState::ShaderRead);
			commands.Transition(*target, Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment);
			Rhi::RenderingAttachmentDesc attachment{};
			attachment.View = targetView.get();
			attachment.Load = Rhi::LoadOp::Clear;
			commands.BeginRendering({ { &attachment, 1 }, nullptr, { 16, 16 } });
			commands.BindGraphicsPipeline(*pipeline);
			commands.BindDescriptorTable(schema.Space, *tables[pass]);
			commands.SetViewport({ 0, 0, 16, 16 });
			commands.SetScissor({ 0, 0, 16, 16 });
			commands.Draw(3);
			commands.EndRendering();
			commands.Transition(*target, Rhi::ResourceState::ColorAttachment, Rhi::ResourceState::CopySource);
			commands.Transition(*readback, pass == 0 ? Rhi::ResourceState::Undefined : Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
			copy.BufferOffset = 0;
			copy.Extent = targetDesc.Extent;
			commands.CopyTextureToBuffer(*target, *readback, copy);
			commands.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
			commands.End();
			frames->SubmitCurrent();
			frames->Drain();
			std::array<std::byte, 16 * 16 * 4> pixels{};
			readback->Read(0, pixels);
			for (std::size_t y = 0; y < 16; ++y)
			{
				for (std::size_t x = 0; x < 16; ++x)
				{
					const auto source = pass * 16 + ((y / 8) * 2 + x / 8) * 4;
					const auto destination = (y * 16 + x) * 4;
					for (std::size_t channel = 0; channel < 4; ++channel)
					{
						SWIM_CHECK(pixels[destination + channel] == static_cast<std::byte>(patterns[source + channel]));
					}
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
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ReflectedTexturesAndTableReplacement", SWIM_TEST_LOCATION, &RunTextureSmoke });
		}
		return true;
	}();

} // namespace
