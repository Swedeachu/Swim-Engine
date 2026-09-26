#include "Engine/Systems/Renderer/Runtime/RenderDevice.h"

#include "Engine/Platform/Window.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"

#include <iostream>
#include <stdexcept>

namespace Engine
{
	RenderDevice::RenderDevice(const RenderDeviceDesc& desc) : window(desc.Window), vsync(desc.VSync)
	{
		Swim::Rhi::GraphicsSystemDesc systemDesc;
		systemDesc.Validation = desc.Validation ? Swim::Rhi::ValidationMode::IfAvailable : Swim::Rhi::ValidationMode::Disabled;
		systemDesc.EchoDiagnostics = true;
		graphics = Swim::RhiVulkan::CreateGraphicsSystem(systemDesc);
		if (!graphics || graphics->GetAdapterCount() == 0)
		{
			throw std::runtime_error("RenderDevice: no Vulkan adapter is available");
		}
		// The first adapter with bindless descriptors (the renderer's hard requirement).
		std::uint32_t chosen = UINT32_MAX;
		for (std::uint32_t i = 0; i < graphics->GetAdapterCount(); ++i)
		{
			if (graphics->GetAdapter(i).GetInfo().Capabilities.BindlessDescriptors)
			{
				chosen = i;
				break;
			}
		}
		if (chosen == UINT32_MAX)
		{
			throw std::runtime_error("RenderDevice: no adapter supports bindless descriptors");
		}
		auto& adapter = graphics->GetAdapter(chosen);
		adapterInfo = &adapter.GetInfo();
		device = adapter.CreateDevice();
		if (!device)
		{
			throw std::runtime_error("RenderDevice: device creation failed on " + adapterInfo->Name);
		}
		executor = std::make_unique<Swim::Render::RenderGraphExecutor>(*device);
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
			acquired = device->CreateGpuSemaphore();
			CreatePresentSemaphores();
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
		ready.clear();
		acquired.reset();
		swapchain.reset();
		executor.reset();
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

	bool RenderDevice::Rebuild()
	{
		Swim::Rhi::Extent2D extent = requestedExtent;
		if (!resizeRequested && window)
		{
			const auto pixels = window->GetPixelSize();
			extent = { pixels.Width, pixels.Height };
		}
		// Imported swapchain views retire with the executor's pooled resources.
		executor->Trim();
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

	RenderDevice::Frame RenderDevice::Acquire()
	{
		Frame frame;
		if (!swapchain)
		{
			frame.Valid = true;
			return frame;
		}
		if ((resizeRequested || needsRebuild) && !Rebuild())
		{
			return frame;
		}
		const auto result = swapchain->AcquireNextImage(*acquired);
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
		waits[0] = acquired.get();
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
		const std::array<Swim::Rhi::Semaphore*, 1> presentWaits{ ready[frame.Image].get() };
		const bool ok = swapchain->Present(device->GetQueue(Swim::Rhi::QueueType::Graphics), frame.Image, presentWaits);
		presented[frame.Image] = true;
		if (!ok || frame.Suboptimal)
		{
			needsRebuild = true;
		}
		return ok;
	}

	void RenderDevice::WaitIdle()
	{
		if (executor)
		{
			executor->Wait();
		}
		if (device)
		{
			// Presentation waits are not covered by the render completion timeline.
			device->GetQueue(Swim::Rhi::QueueType::Graphics).WaitIdle();
		}
	}
} // namespace Engine
