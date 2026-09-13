#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#ifdef SWIM_RHI_TEXTURE_SPIRV_PATH
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#endif
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

namespace
{
	void RunRenderGraphSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_RHI_TEXTURE_SPIRV_PATH
		SWIM_REQUIRE_MESSAGE(false, "RenderGraph desktop smoke requires generated Slang texture artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;
		using Q = Rhi::QueueType;

		const auto reflection = ShaderCompiler::LoadSlangReflectionJson(SWIM_RHI_TEXTURE_REFLECTION_PATH);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		const auto interface = ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(interface, interface.Error);
		SWIM_REQUIRE_EQUAL(interface.Interface.DescriptorSchemas.size(), 1u);
		const auto& schema = interface.Interface.DescriptorSchemas.front();
		std::uint32_t textureBinding = UINT32_MAX, samplerBinding = UINT32_MAX;
		for (const auto& binding : schema.Bindings)
		{
			if (binding.Type == Rhi::DescriptorType::SampledTexture)
			{
				textureBinding = binding.Binding;
			}
			if (binding.Type == Rhi::DescriptorType::Sampler)
			{
				samplerBinding = binding.Binding;
			}
		}
		SWIM_REQUIRE(textureBinding != UINT32_MAX && samplerBinding != UINT32_MAX);

		std::ifstream file(SWIM_RHI_TEXTURE_SPIRV_PATH, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		const auto size = file.tellg();
		SWIM_REQUIRE(size > 0);
		std::vector<std::byte> bytes(static_cast<std::size_t>(size));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file);

		Platform::PlatformSystem platform;
		SWIM_REQUIRE(platform.Initialize());
		Platform::WindowDesc windowDesc{};
		windowDesc.Title = "Swim RenderGraph offscreen / post / present";
		windowDesc.Width = 320;
		windowDesc.Height = 240;
		windowDesc.GraphicsSupport = Platform::WindowGraphicsSupport::Vulkan;
		auto window = platform.GetWindowSystem().Create(windowDesc);
		SWIM_REQUIRE(window);
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const std::array<Rhi::ShaderStageArtifact, 2> stages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", bytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytes } } };
		auto program = device->CreateShaderProgram(
			{ stages, { interface.Interface.DescriptorSchemas, interface.Interface.PushConstants }, "Graph post" });
		SWIM_REQUIRE(program);
		auto layout = device->CreatePipelineLayout({ program.get(), "Graph post layout" });
		SWIM_REQUIRE(layout);
		auto sampler = device->CreateSampler({});
		SWIM_REQUIRE(sampler);
		auto swapchain = device->CreateSwapchain(*window, {});
		SWIM_REQUIRE(swapchain);

		const auto createPipeline = [&](Rhi::Format format)
		{
			Rhi::GraphicsPipelineDesc desc{};
			desc.Program = program.get();
			desc.Layout = layout.get();
			desc.ColorFormats = { &format, 1 };
			desc.DepthStencil.DepthTest = desc.DepthStencil.DepthWrite = false;
			desc.Raster.Cull = Rhi::CullMode::None;
			return device->CreateGraphicsPipeline(desc);
		};
		auto postPipeline = createPipeline(Rhi::Format::RGBA8Unorm);
		SWIM_REQUIRE(postPipeline);
		auto presentPipeline = createPipeline(swapchain->GetFormat());
		SWIM_REQUIRE(presentPipeline);

		auto acquired = device->CreateGpuSemaphore();
		SWIM_REQUIRE(acquired);
		std::vector<std::unique_ptr<Rhi::Semaphore>> ready;
		for (std::uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
		{
			ready.push_back(device->CreateGpuSemaphore());
			SWIM_REQUIRE(ready.back());
		}

		auto readback =
			device->CreateBuffer({ 16 * 16 * 4, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, "Graph readback" });
		SWIM_REQUIRE(readback);
		std::vector<bool> presented(swapchain->GetImageCount(), false);

		// Declared after all imports/captured pipelines so destruction drains first.
		RenderGraphExecutor executor(*device);
		try
		{
			for (std::uint32_t frame = 0; frame < 4; ++frame)
			{

				executor.Wait();
				const auto image = swapchain->AcquireNextImage(*acquired);
				SWIM_REQUIRE(image.HasImage());
				const auto extent = swapchain->GetExtent();

				RenderGraph graph;
				Rhi::TextureDesc desc{};
				desc.Extent = { 16, 16, 1 };
				desc.PixelFormat = Rhi::Format::RGBA8Unorm;
				desc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferSource;
				desc.DebugName = "Graph offscreen";
				auto offscreen = graph.CreateTexture(desc);
				desc.DebugName = "Graph post result";
				auto post = graph.CreateTexture(desc);
				auto surface = graph.ImportTexture(
					swapchain->GetImageView(image.ImageIndex).GetTexture(), presented[image.ImageIndex] ? S::Present : S::Undefined);
				auto output = graph.ImportBuffer(*readback, frame ? S::HostRead : S::Undefined);

				const bool green = frame % 2 != 0;

				graph.AddPass(
					"Offscreen", Q::Graphics,
					[&](auto& b)
					{
						b.Write(offscreen, S::ColorAttachment);
					},
					[=](RenderCommandContext& c)
					{
						Rhi::RenderingAttachmentDesc attachment{};
						attachment.View = &c.CreateView(offscreen);
						attachment.Load = Rhi::LoadOp::Clear;
						attachment.Clear.Value = green ? std::array{ 0.0f, 1.0f, 0.0f, 1.0f } : std::array{ 1.0f, 0.0f, 1.0f, 1.0f };
						c.Commands().BeginRendering({ { &attachment, 1 }, nullptr, { 16, 16 } });
						c.Commands().EndRendering();
					});

				const auto draw = [&](RenderCommandContext& c, GraphTexture input, GraphTexture target, Rhi::GraphicsPipeline& pipeline,
									  Rhi::Extent2D area)
				{
					auto& sourceView = c.CreateView(input);
					auto& targetView = c.CreateView(target);
					auto table = c.Device().CreateDescriptorTable({ layout.get(), schema.Space, 0, "Graph sampled input" });
					SWIM_REQUIRE(table);
					std::array<Rhi::DescriptorWrite, 2> writes{};
					writes[0].Binding = textureBinding;
					writes[0].TextureResource = &sourceView;
					writes[1].Binding = samplerBinding;
					writes[1].SamplerResource = sampler.get();
					table->Write(writes);
					auto& retainedTable = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));

					Rhi::RenderingAttachmentDesc attachment{};
					attachment.View = &targetView;
					attachment.Load = Rhi::LoadOp::Clear;
					auto& commands = c.Commands();
					commands.BeginRendering({ { &attachment, 1 }, nullptr, area });
					commands.BindGraphicsPipeline(pipeline);
					commands.BindDescriptorTable(schema.Space, retainedTable);
					commands.SetViewport({ 0, 0, static_cast<float>(area.Width), static_cast<float>(area.Height) });
					commands.SetScissor({ 0, 0, area.Width, area.Height });
					commands.Draw(3);
					commands.EndRendering();
				};

				graph.AddPass(
					"Post", Q::Graphics,
					[&](auto& b)
					{
						b.Read(offscreen, S::ShaderRead);
						b.Write(post, S::ColorAttachment);
					},
					[&](auto& c)
					{
						draw(c, offscreen, post, *postPipeline, { 16, 16 });
					});

				graph.AddPass(
					"Present", Q::Graphics,
					[&](auto& b)
					{
						b.Read(post, S::ShaderRead);
						b.Write(surface, S::ColorAttachment);
					},
					[&](auto& c)
					{
						draw(c, post, surface, *presentPipeline, extent);
					});

				graph.AddPass(
					"Readback", Q::Transfer,
					[&](auto& b)
					{
						b.Read(post, S::CopySource);
						b.Write(output, S::CopyDestination);
					},
					[&](auto& c)
					{
						Rhi::BufferTextureCopyRegion copy{};
						copy.Extent = { 16, 16, 1 };
						c.Commands().CopyTextureToBuffer(c.Get(post), c.Get(output), copy);
					});

				graph.Export(surface, S::Present);
				graph.Export(output, S::HostRead);
				auto plan = graph.Compile();
				SWIM_REQUIRE_EQUAL(plan.GetSchedule().size(), 4u);
				SWIM_CHECK(plan.Dump().find("Offscreen") != std::string::npos);

				std::array waits{ acquired.get() };
				std::array signals{ ready[image.ImageIndex].get() };
				Rhi::SubmitDesc submit{};
				submit.WaitSemaphores = waits;
				submit.SignalSemaphores = signals;
				const auto completion = executor.Execute(plan, submit);
				SWIM_REQUIRE(swapchain->Present(device->GetQueue(Q::Graphics), image.ImageIndex, signals));
				presented[image.ImageIndex] = true;

				executor.Wait();
				SWIM_CHECK_EQUAL(executor.GetPooledResourceCount(), 2u);
				std::array<std::byte, 16 * 16 * 4> pixels{};
				readback->Read(0, pixels);
				for (std::size_t i = 0; i < pixels.size(); ++i)
				{
					const bool full = i % 4 == 3 || (green ? i % 4 == 1 : i % 4 != 1);
					SWIM_CHECK_EQUAL(pixels[i], full ? std::byte{ 255 } : std::byte{ 0 });
				}

				const auto timings = executor.ReadTimings();
				SWIM_REQUIRE_EQUAL(timings.size(), 4u);
				for (const auto& timing : timings)
				{
					if (device->GetQueue(Q::Graphics).GetTimestampInfo().IsSupported())
					{
						SWIM_CHECK(timing.Nanoseconds.has_value());
					}
				}

				if (frame == 1)
				{
					// Imported graph views must retire before replacing their swapchain.
					executor.Trim();
					window->SetSize({ 400, 300 });
					const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
					bool resized = false;
					do
					{
						platform.PumpEvents({}, {});
						const auto logical = window->GetLogicalSize();
						resized = logical.Width == 400 && logical.Height == 300;
						if (!resized)
						{
							std::this_thread::sleep_for(std::chrono::milliseconds(10));
						}
					} while (!resized && std::chrono::steady_clock::now() < deadline);
					SWIM_REQUIRE_MESSAGE(resized, "Graph window resize did not complete within five seconds");
					const auto pixels = window->GetPixelSize();
					SWIM_REQUIRE(swapchain->Resize({ pixels.Width, pixels.Height }, completion));

					ready.clear();
					for (std::uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
					{
						ready.push_back(device->CreateGpuSemaphore());
						SWIM_REQUIRE(ready.back());
					}
					presented.assign(swapchain->GetImageCount(), false);
					presentPipeline = createPipeline(swapchain->GetFormat());
					SWIM_REQUIRE(presentPipeline);
				}
			}
			executor.Trim();
			// Presentation waits are not covered by the render completion timeline.
			device->GetQueue(Q::Graphics).WaitIdle();
		}
		catch (...)
		{

			executor.Wait();
			device->GetQueue(Q::Graphics).WaitIdle();
			throw;
		}
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "RenderGraphOffscreenPostPresentAndReuse", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunRenderGraphSmoke);
				} });
		}
		return true;
	}();
} // namespace
