#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace
{

	void RunClearAndTransferSmoke()
	{
		using namespace Swim;
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "RHI smoke requires a working SDL desktop video driver");
		Platform::WindowDesc windowDesc{};
		windowDesc.Title = "Swim RHI clear / transfer smoke";
		windowDesc.Width = 320;
		windowDesc.Height = 240;
		windowDesc.GraphicsSupport = Platform::WindowGraphicsSupport::Vulkan;
		auto window = platform.GetWindowSystem().Create(windowDesc);
		SWIM_REQUIRE_MESSAGE(window, "RHI smoke could not create a Vulkan window");
		auto graphics = RhiVulkan::CreateGraphicsSystem();
		SWIM_REQUIRE_MESSAGE(graphics, "RHI smoke requires an adapter supporting the full Swim Vulkan 1.3 baseline");
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		Rhi::TextureDesc textureDesc{};
		textureDesc.Extent = { 8, 8, 1 };
		textureDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
		textureDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination;
		auto source = device->CreateTexture(textureDesc);
		auto destination = device->CreateTexture(textureDesc);
		SWIM_REQUIRE(source && destination);
		auto view = device->CreateTextureView(*source, {});
		SWIM_REQUIRE(view);
		constexpr std::size_t byteCount = 8 * 8 * 4;
		auto upload = device->CreateBuffer({ byteCount, Rhi::BufferUsage::TransferSource, Rhi::MemoryPreference::CpuToGpu, {} });
		auto readback = device->CreateBuffer({ byteCount, Rhi::BufferUsage::TransferDestination, Rhi::MemoryPreference::GpuToCpu, {} });
		SWIM_REQUIRE(upload && readback);
		auto swapchain = device->CreateSwapchain(*window, {});
		SWIM_REQUIRE(swapchain);
		std::array<std::unique_ptr<Rhi::Semaphore>, 2> acquired;
		std::vector<std::unique_ptr<Rhi::Semaphore>> presentReady;
		for (auto& semaphore : acquired)
		{
			semaphore = device->CreateGpuSemaphore();
			SWIM_REQUIRE(semaphore);
		}
		for (std::uint32_t index = 0; index < swapchain->GetImageCount(); ++index)
		{
			presentReady.push_back(device->CreateGpuSemaphore());
			SWIM_REQUIRE(presentReady.back());
		}
		// Declared last so GPU work drains before resources unwind on failure.
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2 });
		SWIM_REQUIRE(frames);
		Rhi::BufferTextureCopyRegion copy{};
		copy.Extent = textureDesc.Extent;

		frames->BeginFrame();
		auto& clear = frames->CreateCommandList();
		clear.Begin();
		clear.Transition(*source, Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment);
		Rhi::RenderingAttachmentDesc attachment{};
		attachment.View = view.get();
		attachment.Load = Rhi::LoadOp::Clear;
		attachment.Clear.Value = { 1.0f, 0.0f, 1.0f, 1.0f };
		clear.BeginRendering({ { &attachment, 1 }, nullptr, { 8, 8 } });
		clear.EndRendering();
		clear.Transition(*source, Rhi::ResourceState::ColorAttachment, Rhi::ResourceState::CopySource);
		clear.Transition(*readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::CopyDestination);
		clear.CopyTextureToBuffer(*source, *readback, copy);
		clear.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
		clear.End();
		frames->SubmitCurrent();
		frames->Drain();
		std::array<std::byte, byteCount> pixels{};
		readback->Read(0, pixels);
		for (std::size_t index = 0; index < pixels.size(); index += 4)
		{
			SWIM_CHECK(pixels[index] == std::byte{ 255 } && pixels[index + 1] == std::byte{ 0 } &&
				pixels[index + 2] == std::byte{ 255 } && pixels[index + 3] == std::byte{ 255 });
		}

		std::array<std::byte, byteCount> pattern{};
		for (std::size_t index = 0; index < pattern.size(); ++index)
		{
			pattern[index] = static_cast<std::byte>((index * 37) & 255);
		}
		upload->Write(0, pattern);
		frames->BeginFrame();
		auto& transfer = frames->CreateCommandList();
		transfer.Begin();
		transfer.Transition(*upload, Rhi::ResourceState::HostWrite, Rhi::ResourceState::CopySource);
		transfer.Transition(*source, Rhi::ResourceState::CopySource, Rhi::ResourceState::CopyDestination);
		transfer.CopyBufferToTexture(*upload, *source, copy);
		transfer.Transition(*source, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::CopySource);
		transfer.Transition(*destination, Rhi::ResourceState::Undefined, Rhi::ResourceState::CopyDestination);
		Rhi::TextureCopyRegion imageCopy{};
		imageCopy.Extent = textureDesc.Extent;
		transfer.CopyTexture(*source, *destination, imageCopy);
		transfer.Transition(*destination, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::CopySource);
		transfer.Transition(*readback, Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
		transfer.CopyTextureToBuffer(*destination, *readback, copy);
		transfer.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
		transfer.End();
		frames->SubmitCurrent();
		frames->Drain();
		readback->Read(0, pixels);
		SWIM_CHECK(pixels == pattern);

		frames->BeginFrame();
		auto& bufferCopy = frames->CreateCommandList();
		bufferCopy.Begin();
		bufferCopy.Transition(*readback, Rhi::ResourceState::HostRead, Rhi::ResourceState::CopyDestination);
		bufferCopy.CopyBuffer(*upload, *readback, { 0, 0, byteCount });
		bufferCopy.Transition(*readback, Rhi::ResourceState::CopyDestination, Rhi::ResourceState::HostRead);
		bufferCopy.End();
		frames->SubmitCurrent();
		frames->Drain();
		readback->Read(0, pixels);
		SWIM_CHECK(pixels == pattern);

		auto& queue = device->GetQueue(Rhi::QueueType::Graphics);
		try
		{
			for (std::uint32_t frame = 0; frame < 6; ++frame)
			{
				platform.PumpEvents({}, {});
				auto& context = frames->BeginFrame();
				const auto image = swapchain->AcquireNextImage(*acquired[context.Index]);
				SWIM_REQUIRE_MESSAGE(!image.OutOfDate, "Swapchain became out of date during the bounded clear smoke");
				auto& backbuffer = swapchain->GetImageView(image.ImageIndex);
				auto& commands = frames->CreateCommandList();
				commands.Begin();
				// Every frame discards the old contents, so Undefined is intentional.
				commands.Transition(backbuffer.GetTexture(), Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment);
				attachment.View = &backbuffer;
				commands.BeginRendering({ { &attachment, 1 }, nullptr, swapchain->GetExtent() });
				commands.EndRendering();
				commands.Transition(backbuffer.GetTexture(), Rhi::ResourceState::ColorAttachment, Rhi::ResourceState::Present);
				commands.End();
				std::array<Rhi::CommandList*, 1> lists{ &commands };
				std::array<Rhi::Semaphore*, 1> waits{ acquired[context.Index].get() };
				// Presentation waits are reused by acquired image, not frame slot.
				std::array<Rhi::Semaphore*, 1> signals{ presentReady[image.ImageIndex].get() };
				Rhi::SubmitDesc submit{};
				submit.CommandLists = lists;
				submit.WaitSemaphores = waits;
				submit.SignalSemaphores = signals;
				frames->SubmitCurrent(submit);
				SWIM_REQUIRE(swapchain->Present(queue, image.ImageIndex, signals));
			}
			frames->Drain();
			// Render timeline completion does not retire WSI semaphore waits.
			queue.WaitIdle();
		}
		catch (...)
		{
			frames->Drain();
			queue.WaitIdle();
			throw;
		}
	}

	// Default suites run on build machines without a desktop/GPU. Opting in
	// registers the real-driver gate; unavailable Vulkan is then a test failure.
	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ClearTransferAndPresent", SWIM_TEST_LOCATION, &RunClearAndTransferSmoke });
		}
		return true;
	}();

} // namespace
