#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RHI/RhiFrameLifetime.h"
#include "Tests/Framework/Test.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

	using namespace Swim;
	using Clock = std::chrono::steady_clock;

	template <typename Predicate>
	void PumpUntil(Platform::PlatformSystem& platform, Predicate ready)
	{
		const auto deadline = Clock::now() + std::chrono::seconds(5);
		do
		{
			platform.PumpEvents({}, {});
			if (ready())
			{
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		} while (Clock::now() < deadline);
		SWIM_REQUIRE_MESSAGE(false, "Window manager did not complete the requested lifecycle transition within five seconds");
	}

	void RunWindowLifecycleSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Window lifecycle smoke requires a desktop video driver and window manager");
		Platform::WindowDesc desc{};
		desc.Title = "Swim RHI resize / minimize / restore smoke";
		desc.Width = 320;
		desc.Height = 240;
		desc.GraphicsSupport = Platform::WindowGraphicsSupport::Vulkan;
		auto window = platform.GetWindowSystem().Create(desc);
		SWIM_REQUIRE(window);
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE_MESSAGE(graphics, "Window smoke requires the full Swim Vulkan 1.3 baseline");
		SWIM_REQUIRE_MESSAGE(graphics->IsValidationEnabled(), "Smoke requires active Vulkan validation");
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		// First creation while minimized must succeed as a dormant object, with
		// no zero-sized native swapchain and no acquire semaphore signal.
		SWIM_REQUIRE(window->Minimize());
		PumpUntil(platform, [&] { return window->IsMinimized(); });
		auto swapchain = device->CreateSwapchain(*window, {});
		SWIM_REQUIRE(swapchain);
		SWIM_CHECK_EQUAL(swapchain->GetImageCount(), 0u);
		std::array<std::unique_ptr<Rhi::Semaphore>, 2> acquired;
		for (auto& semaphore : acquired)
		{
			semaphore = device->CreateGpuSemaphore();
			SWIM_REQUIRE(semaphore);
		}
		std::vector<std::unique_ptr<Rhi::Semaphore>> presentReady;
		auto frames = Rhi::FrameContextRing::Create(*device, { Rhi::QueueType::Graphics, 2 });
		SWIM_REQUIRE(frames);
		auto& queue = device->GetQueue(Rhi::QueueType::Graphics);

		auto rebuild = [&]
		{
			const auto size = window->GetPixelSize();
			if (!swapchain->Resize({ size.Width, size.Height }, frames->GetLastSubmittedPoint()))
			{
				return false;
			}
			// Resize retires the old generation before these presentation waits are
			// destroyed. A changed image count gets a fresh per-image semaphore set.
			presentReady.clear();
			for (std::uint32_t index = 0; index < swapchain->GetImageCount(); ++index)
			{
				presentReady.push_back(device->CreateGpuSemaphore());
				SWIM_REQUIRE(presentReady.back());
			}
			return true;
		};

		auto checkSuspended = [&]
		{
			const auto submitted = frames->GetLastSubmittedValue();
			for (unsigned pump = 0; pump < 4; ++pump)
			{
				platform.PumpEvents({}, {});
				auto& frame = frames->BeginFrame();
				const auto result = swapchain->AcquireNextImage(*acquired[frame.Index]);
				SWIM_CHECK(result.Suspended && !result.HasImage());
				frames->CancelFrame();
			}
			SWIM_CHECK_EQUAL(frames->GetLastSubmittedValue(), submitted);
		};

		auto drawFrames = [&]
		{
			const auto deadline = Clock::now() + std::chrono::seconds(10);
			unsigned rendered = 0;
			while (rendered < 6 && Clock::now() < deadline)
			{
				platform.PumpEvents({}, {});
				auto& frame = frames->BeginFrame();
				const auto image = swapchain->AcquireNextImage(*acquired[frame.Index]);
				if (!image.HasImage())
				{
					frames->CancelFrame();
					if (image.OutOfDate || image.Suspended)
					{
						rebuild();
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}
				auto& view = swapchain->GetImageView(image.ImageIndex);
				auto& commands = frames->CreateCommandList();
				commands.Begin();
				commands.BeginDebugLabel("RunWindowLifecycleSmoke: commands", { 0.2f, 0.6f, 0.9f, 1.0f });
				commands.Transition(view.GetTexture(), Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment);
				Rhi::RenderingAttachmentDesc color{};
				color.View = &view;
				color.Load = Rhi::LoadOp::Clear;
				color.Clear.Value = { 0.1f, rendered / 6.0f, 0.7f, 1.0f };
				commands.BeginRendering({ { &color, 1 }, nullptr, swapchain->GetExtent() });
				commands.EndRendering();
				commands.Transition(view.GetTexture(), Rhi::ResourceState::ColorAttachment, Rhi::ResourceState::Present);
				commands.EndDebugLabel();
				commands.End();
				std::array<Rhi::CommandList*, 1> lists{ &commands };
				std::array<Rhi::Semaphore*, 1> waits{ acquired[frame.Index].get() };
				std::array<Rhi::Semaphore*, 1> signals{ presentReady.at(image.ImageIndex).get() };
				Rhi::SubmitDesc submit{};
				submit.CommandLists = lists;
				submit.WaitSemaphores = waits;
				submit.SignalSemaphores = signals;
				frames->SubmitCurrent(submit);
				// Suboptimal acquisition is still submitted/presented before rebuilding.
				if (!swapchain->Present(queue, image.ImageIndex, signals))
				{
					rebuild();
				}
				else
				{
					++rendered;
				}
			}
			SWIM_REQUIRE_MESSAGE(rendered == 6, "Swapchain failed to resume presentation within ten seconds");
		};

		try
		{
			checkSuspended();
			SWIM_REQUIRE(window->Restore());
			PumpUntil(platform, [&] { return !window->IsMinimized(); });
			PumpUntil(platform, rebuild);
			drawFrames();
			for (Platform::Extent2D size : { Platform::Extent2D{ 480, 270 }, { 256, 384 }, { 320, 240 } })
			{
				window->SetSize(size);
				PumpUntil(platform, [&]
				{
					const auto actual = window->GetLogicalSize();
					return actual.Width == size.Width && actual.Height == size.Height;
				});
				PumpUntil(platform, rebuild);
				drawFrames();
				// Explicit zero extent also suspends when SDL reports nonzero pixels.
				SWIM_CHECK(!swapchain->Resize({}, frames->GetLastSubmittedPoint()));
				checkSuspended();
				SWIM_REQUIRE(window->Minimize());
				PumpUntil(platform, [&] { return window->IsMinimized(); });
				checkSuspended();
				SWIM_REQUIRE(window->Restore());
				PumpUntil(platform, [&] { return !window->IsMinimized(); });
				PumpUntil(platform, rebuild);
				drawFrames();
				// Restore/rebuild at the same size must create a usable generation too.
				PumpUntil(platform, rebuild);
				drawFrames();
			}
			frames->Drain();
			queue.WaitIdle();
		}
		catch (...)
		{
			frames->Drain();
			queue.WaitIdle();
			throw;
		}
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ResizeMinimizeRestore", SWIM_TEST_LOCATION, +[] { Swim::Testing::RunValidatedVulkanSmoke(&RunWindowLifecycleSmoke); } });
		}
		return true;
	}();

} // namespace
