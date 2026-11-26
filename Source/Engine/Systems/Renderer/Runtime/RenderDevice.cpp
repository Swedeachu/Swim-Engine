#include "Engine/Systems/Renderer/Runtime/RenderDevice.h"

#include <algorithm>

#include "Engine/Platform/Window.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include "Engine/Platform/Internal/WindowsApi.h"

#include <dxgi.h>
#endif

namespace Engine
{

	namespace
	{

		// The id of the adapter that drives the display the window is on (Windows: DXGI).
		std::optional<std::array<std::uint8_t, 8>> DisplayAdapterLuid(Swim::Platform::Window* window)
		{
#ifdef _WIN32
			const auto handle = window ? window->GetNativeHandle() : Swim::Platform::NativeWindowHandle{};

			if (!handle.IsValid())
			{
				return std::nullopt;
			}

			HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(handle.Window), MONITOR_DEFAULTTONEAREST);
			HMODULE library = LoadLibraryW(L"dxgi.dll");
			using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
			auto* create = library ? reinterpret_cast<CreateFactory>(GetProcAddress(library, "CreateDXGIFactory1")) : nullptr;
			IDXGIFactory1* factory = nullptr;
			std::optional<std::array<std::uint8_t, 8>> found;

			if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
			{
				IDXGIAdapter1* adapter = nullptr;

				for (UINT a = 0; !found && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a)
				{
					IDXGIOutput* output = nullptr;

					for (UINT o = 0; !found && adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o)
					{
						DXGI_OUTPUT_DESC desc{};
						DXGI_ADAPTER_DESC1 adapterDesc{};

						if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor && SUCCEEDED(adapter->GetDesc1(&adapterDesc)))
						{
							std::array<std::uint8_t, 8> luid{};
							std::memcpy(luid.data(), &adapterDesc.AdapterLuid, luid.size());
							found = luid;
						}

						output->Release();
					}

					adapter->Release();
				}

				factory->Release();
			}

			if (library)
			{
				FreeLibrary(library);
			}

			return found;
#else
			(void)window;
			return std::nullopt;
#endif
		}

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
				[](unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			return text;
		}

	} // namespace

	RenderDevice::RenderDevice(const RenderDeviceDesc& desc)
		: window(desc.Window), vsync(desc.VSync), asyncPresent(desc.AsyncPresent && desc.Window)
	{
		Swim::Rhi::GraphicsSystemDesc systemDesc;
		systemDesc.Validation = desc.Validation ? Swim::Rhi::ValidationMode::IfAvailable : Swim::Rhi::ValidationMode::Disabled;
		systemDesc.EchoDiagnostics = true;
		graphics = Swim::RhiVulkan::CreateGraphicsSystem(systemDesc);

		if (!graphics || graphics->GetAdapterCount() == 0)
		{
			throw std::runtime_error("RenderDevice: no Vulkan adapter is available");
		}

		// Among the adapters with bindless descriptors (the renderer's hard requirement), the
		// one RenderDeviceDesc::Adapter asks for.
		const auto displayLuid = DisplayAdapterLuid(window);
		const std::string wanted = Lower(desc.Adapter.empty() ? std::string("auto") : desc.Adapter);
		const auto drivesDisplay = [&](const Swim::Rhi::AdapterInfo& info)
		{
			return displayLuid && info.LuidValid && info.Luid == *displayLuid;
		};
		const auto pick = [&](auto&& accept)
		{
			for (std::uint32_t i = 0; i < graphics->GetAdapterCount(); ++i)
			{
				const auto& info = graphics->GetAdapter(i).GetInfo();

				if (info.Capabilities.BindlessDescriptors && accept(i, info))
				{
					return i;
				}
			}

			return UINT32_MAX;
		};
		std::uint32_t chosen = UINT32_MAX;

		if (wanted == "auto")
		{
			chosen = pick(
				[&](std::uint32_t, const Swim::Rhi::AdapterInfo& info)
				{
					return info.Discrete && drivesDisplay(info);
				});
			chosen = chosen != UINT32_MAX ? chosen
										  : pick(
												[](std::uint32_t, const Swim::Rhi::AdapterInfo& info)
												{
													return info.Discrete;
												});
		}
		else if (wanted == "display")
		{
			chosen = pick(
				[&](std::uint32_t, const Swim::Rhi::AdapterInfo& info)
				{
					return drivesDisplay(info);
				});
		}
		else if (wanted == "discrete" || wanted == "integrated")
		{
			const bool discrete = wanted == "discrete";
			chosen = pick(
				[&](std::uint32_t, const Swim::Rhi::AdapterInfo& info)
				{
					return discrete ? info.Discrete : info.Integrated;
				});
		}
		else
		{
			chosen = pick(
				[&](std::uint32_t i, const Swim::Rhi::AdapterInfo& info)
				{
					return std::to_string(i) == wanted || Lower(info.Name).find(wanted) != std::string::npos;
				});
		}

		if (chosen == UINT32_MAX)
		{
			if (wanted != "auto")
			{
				std::cout << "[Render] no adapter matches \"" << desc.Adapter << "\"; using the default\n";
			}

			chosen = pick(
				[](std::uint32_t, const Swim::Rhi::AdapterInfo&)
				{
					return true;
				});
		}

		if (chosen == UINT32_MAX)
		{
			throw std::runtime_error("RenderDevice: no adapter supports bindless descriptors");
		}

		auto& adapter = graphics->GetAdapter(chosen);
		adapterInfo = &adapter.GetInfo();
		presentStats.Async = asyncPresent;
		deferPresent = asyncPresent && desc.DeferPresent;
		presentStats.Deferred = deferPresent;
		presentStats.CrossAdapter = displayLuid && adapterInfo->LuidValid && adapterInfo->Luid != *displayLuid;
		splitPresent = asyncPresent &&
			(desc.SplitPresent == RenderDeviceDesc::Split::On ||
				(desc.SplitPresent == RenderDeviceDesc::Split::Auto && presentStats.CrossAdapter));
		presentStats.Split = splitPresent;
		device = adapter.CreateDevice();

		if (!device)
		{
			throw std::runtime_error("RenderDevice: device creation failed on " + adapterInfo->Name);
		}

		const std::uint32_t slots = std::clamp(desc.FramesInFlight, 1u, 2u);

		for (std::uint32_t i = 0; i < slots; ++i)
		{
			executors.push_back(std::make_unique<Swim::Render::RenderGraphExecutor>(*device));
		}

		idleTimeline = device->CreateTimeline(0);
		headlessExtent = { desc.Width, desc.Height };

		if (window)
		{
			Swim::Rhi::SwapchainDesc swapchainDesc;
			// The frame arrives sRGB-encoded in RGBA8Unorm; a UNORM swapchain takes it as is.
			swapchainDesc.PreferredFormat = Swim::Rhi::Format::BGRA8Unorm;
			swapchainDesc.ImageCount = 3;
			swapchainDesc.Vsync = vsync;
			swapchain = device->CreateSwapchain(*window, swapchainDesc);

			if (!swapchain)
			{
				throw std::runtime_error("RenderDevice: swapchain creation failed");
			}

			for (std::uint32_t i = 0; i < slots; ++i)
			{
				acquired.push_back(device->CreateGpuSemaphore());
			}

			CreatePresentSemaphores();

			if (asyncPresent)
			{
				// Submissions then never wait behind a blocking present on the caller's thread
				// (SWIM_SUBMIT_THREAD=off submits on the caller's thread: diagnostics, compare the two).
				const char* submitThread = std::getenv("SWIM_SUBMIT_THREAD");
				device->SetSubmissionThread(submitThread == nullptr || std::string_view(submitThread) != "off");

				for (std::uint32_t i = 0; i < slots; ++i)
				{
					presentExecutors.push_back(std::make_unique<Swim::Render::RenderGraphExecutor>(*device));
				}

				presentThread = std::thread(
					[this]
					{
						PresentLoop();
					});
			}
		}
	}

	RenderDevice::~RenderDevice()
	{
		try
		{
			WaitIdle();
		}
		catch (...)
		{
		}

		if (presentThread.joinable())
		{
			{
				std::scoped_lock lock(presentMutex);
				presentStop = true;
			}

			presentSignal.notify_all();
			presentThread.join();
		}

		ready.clear();
		acquired.clear();
		swapchain.reset();
		presentExecutors.clear();
		executors.clear();
		idleTimeline.reset();
		device.reset();
		graphics.reset();
	}

	bool RenderDevice::IsValidationEnabled() const
	{
		return graphics && graphics->IsValidationEnabled();
	}

	std::uint64_t RenderDevice::GetValidationErrorCount() const
	{
		const auto log = graphics ? graphics->GetDiagnostics() : nullptr;
		return log ? log->Snapshot().Errors : 0u;
	}

	void RenderDevice::CreatePresentSemaphores()
	{
		ready.clear();

		for (std::uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
		{
			ready.push_back(device->CreateGpuSemaphore());
		}

		presented.assign(swapchain->GetImageCount(), false);
	}

	Swim::Rhi::Format RenderDevice::GetSwapchainFormat() const
	{
		return swapchain ? swapchain->GetFormat() : Swim::Rhi::Format::RGBA8Unorm;
	}

	Swim::Rhi::Extent2D RenderDevice::GetExtent() const
	{
		return swapchain ? swapchain->GetExtent() : headlessExtent;
	}

	void RenderDevice::RequestResize(std::uint32_t width, std::uint32_t height)
	{
		if (!swapchain)
		{
			if (width && height)
			{
				headlessExtent = { width, height };
			}

			return;
		}

		requestedExtent = { width, height };
		resizeRequested = true;
	}

	void RenderDevice::PresentLoop()
	{
		std::unique_lock lock(presentMutex);

		while (true)
		{
			presentSignal.wait(lock,
				[this]
				{
					return presentQueued || presentStop;
				});

			if (presentStop)
			{
				return;
			}

			const std::uint32_t image = presentImage;
			lock.unlock();
			const auto start = std::chrono::steady_clock::now();
			bool ok = false;

			try
			{
				const std::array<Swim::Rhi::Semaphore*, 1> presentWaits{ ready[image].get() };
				ok = swapchain->Present(device->GetQueue(Swim::Rhi::QueueType::Graphics), image, presentWaits);
			}
			catch (const std::exception& error)
			{
				std::cerr << "[Render] present failed: " << error.what() << '\n';
			}

			const double call = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			lock.lock();
			presentOk = ok;
			presentCallMilliseconds = call;
			presentQueued = false;
			presentSignal.notify_all();
		}
	}

	void RenderDevice::WaitForPresent()
	{
		if (!presentThread.joinable())
		{
			return;
		}

		const auto start = std::chrono::steady_clock::now();
		std::unique_lock lock(presentMutex);
		presentSignal.wait(lock,
			[this]
			{
				return !presentQueued;
			});
		presentStats.WaitMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		presentStats.CallMilliseconds = presentCallMilliseconds;

		if (!presentOk)
		{
			needsRebuild = true;
			presentOk = true;
		}
	}

	void RenderDevice::QueuePresent(std::uint32_t image)
	{
		WaitForPresent();
		{
			std::scoped_lock lock(presentMutex);
			presentImage = image;
			presentQueued = true;
		}

		presentSignal.notify_all();
	}

	void RenderDevice::FlushDeferredPresent()
	{
		if (deferredImage != UINT32_MAX)
		{
			QueuePresent(deferredImage);
			deferredImage = UINT32_MAX;
		}

		WaitForPresent();
	}

	bool RenderDevice::Rebuild()
	{
		Swim::Rhi::Extent2D extent = requestedExtent;

		if (!resizeRequested && window)
		{
			const auto pixels = window->GetPixelSize();
			extent = { pixels.Width, pixels.Height };
		}

		// The present semaphores are replaced below while the present queue may still wait on
		// them (presentation has no completion signal).
		WaitIdle();

		// Imported swapchain views retire with the executors' pooled resources.
		for (auto& executor : executors)
		{
			executor->Trim();
		}

		for (auto& executor : presentExecutors)
		{
			executor->Trim();
		}

		const Swim::Rhi::TimelinePoint safeAfter =
			lastCompletion.Semaphore ? lastCompletion : Swim::Rhi::TimelinePoint{ idleTimeline.get(), 0 };
		const bool rebuilt = swapchain->Resize(extent, safeAfter);

		if (!rebuilt)
		{
			return false; // Suspended (zero extent) or failed: retry next frame.
		}

		resizeRequested = false;
		needsRebuild = false;
		CreatePresentSemaphores();
		return true;
	}

	void RenderDevice::SetLastCompletion(Swim::Rhi::TimelinePoint completion)
	{
		lastCompletion = completion;

		if (completion.Semaphore)
		{
			queued.push_back(completion);
		}
	}

	std::uint32_t RenderDevice::GetQueuedFrames()
	{
		while (!queued.empty() && queued.front().Semaphore->GetCompletedValue() >= queued.front().Value)
		{
			queued.pop_front();
		}

		return static_cast<std::uint32_t>(queued.size());
	}

	bool RenderDevice::WaitForQueuedFrames(std::uint64_t timeoutNanoseconds, std::uint32_t keep)
	{
		const auto count = GetQueuedFrames();

		if (count <= keep)
		{
			return true;
		}

		const auto target = queued[count - 1 - keep];

		if (!target.Semaphore->Wait(target.Value, timeoutNanoseconds))
		{
			return false;
		}

		queued.erase(queued.begin(), queued.begin() + (count - keep));
		return true;
	}

	void RenderDevice::SetPresentMode(Swim::Rhi::PresentMode mode)
	{
		if (swapchain)
		{
			swapchain->SetPresentMode(mode);
			needsRebuild = true;
		}
	}

	Swim::Rhi::PresentMode RenderDevice::GetPresentMode() const
	{
		return swapchain ? swapchain->GetPresentMode() : Swim::Rhi::PresentMode::Fifo;
	}

	RenderDevice::Frame RenderDevice::Acquire(bool late)
	{
		Frame frame;

		if (!swapchain)
		{
			frame.Valid = true;
			return frame;
		}

		// The swapchain is not used by two threads at once: the last present ends first.
		presentStats.WaitMilliseconds = 0.0;
		WaitForPresent();

		if (resizeRequested || needsRebuild)
		{
			if (late)
			{
				return frame;
			}

			FlushDeferredPresent();

			if (!Rebuild())
			{
				return frame;
			}
		}

		const auto result = swapchain->AcquireNextImage(*acquired[slot]);

		if (result.OutOfDate)
		{
			needsRebuild = true;
			return frame;
		}

		if (!result.HasImage())
		{
			return frame;
		}

		frame.Valid = true;
		frame.Image = result.ImageIndex;
		frame.Target = &swapchain->GetImageView(result.ImageIndex).GetTexture();
		frame.Presented = presented[result.ImageIndex];
		frame.Suboptimal = result.Suboptimal;
		return frame;
	}

	Swim::Rhi::SubmitDesc RenderDevice::GetSubmit(const Frame& frame)
	{
		Swim::Rhi::SubmitDesc submit{};

		if (!swapchain || !frame.Valid || frame.Image == UINT32_MAX)
		{
			return submit;
		}

		waits[0] = acquired[slot].get();
		signals[0] = ready[frame.Image].get();
		submit.WaitSemaphores = waits;
		submit.SignalSemaphores = signals;
		return submit;
	}

	bool RenderDevice::Present(const Frame& frame)
	{
		if (!swapchain || !frame.Valid || frame.Image == UINT32_MAX)
		{
			return true;
		}

		presented[frame.Image] = true;

		if (presentThread.joinable())
		{
			std::uint32_t image = frame.Image;

			if (deferPresent)
			{
				std::swap(image, deferredImage); // Present the previous frame; keep this one.
			}

			if (image != UINT32_MAX)
			{
				QueuePresent(image);
			}

			if (frame.Suboptimal)
			{
				needsRebuild = true;
			}

			return true; // A failure is taken at the next WaitForPresent.
		}

		const auto start = std::chrono::steady_clock::now();
		const std::array<Swim::Rhi::Semaphore*, 1> presentWaits{ ready[frame.Image].get() };
		const bool ok = swapchain->Present(device->GetQueue(Swim::Rhi::QueueType::Graphics), frame.Image, presentWaits);
		presentStats.CallMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

		if (!ok || frame.Suboptimal)
		{
			needsRebuild = true;
		}

		return ok;
	}

	void RenderDevice::WaitIdle()
	{
		FlushDeferredPresent();

		for (auto& executor : executors)
		{
			executor->Wait();
		}

		for (auto& executor : presentExecutors)
		{
			executor->Wait();
		}

		if (device)
		{
			// Presentation waits are not covered by the render completion timeline, and may sit
			// on a separate present queue: wait for the whole device.
			device->WaitIdle();
		}
	}

} // namespace Engine
