#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Swapchain.h"

#include "Engine/Platform/Window.h"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace Swim::RhiD3D12
{

	namespace
	{

		HWND WindowHandle(Platform::Window& window)
		{
			const auto handle = window.GetNativeHandle();

			if (!handle.IsValid() || handle.Type != Platform::NativeWindowType::Win32)
			{
				throw std::runtime_error("D3D12: swapchains need a Win32 window");
			}

			return static_cast<HWND>(handle.Window);
		}

		Rhi::Extent2D ClientExtent(HWND window)
		{
			RECT rect{};
			GetClientRect(window, &rect);
			return { static_cast<std::uint32_t>(std::max<LONG>(rect.right - rect.left, 0)),
				static_cast<std::uint32_t>(std::max<LONG>(rect.bottom - rect.top, 0)) };
		}

		// Whether the display showing the window is in HDR (PQ) mode.
		bool WindowOnHdrDisplay(IDXGIFactory6* factory, HWND window)
		{
			const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
			ComPtr<IDXGIAdapter1> adapter;

			for (UINT a = 0; factory && SUCCEEDED(factory->EnumAdapters1(a, &adapter)); ++a, adapter.Reset())
			{
				ComPtr<IDXGIOutput> output;

				for (UINT o = 0; SUCCEEDED(adapter->EnumOutputs(o, &output)); ++o, output.Reset())
				{
					ComPtr<IDXGIOutput6> output6;
					DXGI_OUTPUT_DESC1 desc{};

					if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)) && desc.Monitor == monitor)
					{
						return desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
					}
				}
			}

			return false;
		}

		// HDR pairs only while the window's display is in HDR mode, in the Vulkan backend's order.
		Rhi::SwapchainSupport Support(IDXGIFactory6* factory, HWND window)
		{
			Rhi::SwapchainSupport support;
			support.PresentationSupported = true;
			using F = Rhi::Format;

			if (WindowOnHdrDisplay(factory, window))
			{
				support.Formats.push_back({ F::RGB10A2Unorm, Rhi::SwapchainColorSpace::Hdr10St2084 });
				support.Formats.push_back({ F::RGBA16Float, Rhi::SwapchainColorSpace::ExtendedSrgbLinear });
			}

			for (const auto format : { F::BGRA8UnormSrgb, F::RGBA8UnormSrgb, F::BGRA8Unorm, F::RGBA8Unorm })
			{
				support.Formats.push_back({ format, Rhi::SwapchainColorSpace::SrgbNonlinear });
			}

			return support;
		}

		// The Vulkan backend's choice: the preferred format within the mode's color spaces, else
		// the first supported pair (10-bit PQ before linear FP16).
		std::optional<Rhi::SwapchainSurfaceFormat> SelectFormat(const Rhi::SwapchainSupport& support, const Rhi::SwapchainDesc& desc)
		{
			const bool hdr = desc.ColorMode != Rhi::SwapchainColorMode::Sdr && support.SupportsHdr();

			if (!hdr && desc.ColorMode == Rhi::SwapchainColorMode::RequireHdr)
			{
				return {};
			}

			std::optional<Rhi::SwapchainSurfaceFormat> fallback;

			for (const auto& pair : support.Formats)
			{
				if (pair.IsHdr() != hdr)
				{
					continue;
				}

				if (pair.PixelFormat == desc.PreferredFormat)
				{
					return pair;
				}

				if (!fallback)
				{
					fallback = pair;
				}
			}

			return fallback;
		}

		DXGI_COLOR_SPACE_TYPE ToDxgiColorSpace(Rhi::SwapchainColorSpace colorSpace)
		{
			switch (colorSpace)
			{
			case Rhi::SwapchainColorSpace::Hdr10St2084:
				return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
			case Rhi::SwapchainColorSpace::ExtendedSrgbLinear:
				return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
			default:
				return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
			}
		}

		// The swapchain buffers' storage format: flip model has no sRGB buffers, the views add it.
		DXGI_FORMAT BufferFormat(Rhi::Format format)
		{
			switch (format)
			{
			case Rhi::Format::BGRA8UnormSrgb:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			case Rhi::Format::RGBA8UnormSrgb:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			default:
				return ToDxgiFormat(format);
			}
		}

	} // namespace

	Rhi::SwapchainSupport QueryD3D12SwapchainSupport(const D3D12DeviceState& state, Platform::Window& window)
	{
		return Support(state.Factory.Get(), WindowHandle(window));
	}

	bool D3D12SwapchainSupportsColorMode(const D3D12DeviceState& state, Platform::Window& window, const Rhi::SwapchainDesc& desc)
	{
		return SelectFormat(QueryD3D12SwapchainSupport(state, window), desc).has_value();
	}

	D3D12Swapchain::D3D12Swapchain(
		std::shared_ptr<D3D12DeviceState> stateValue, D3D12Queue& queueValue, Platform::Window& windowValue, const Rhi::SwapchainDesc& desc)
		: state(std::move(stateValue)), queue(queueValue), window(WindowHandle(windowValue)), vsync(desc.Vsync), wantedMode(desc.Mode),
		  request(desc)
	{
		bufferCount = std::clamp<std::uint32_t>(desc.ImageCount, 2u, DXGI_MAX_SWAP_CHAIN_BUFFERS);
		// Created minimized: a dormant object until the first non-empty Resize.
		const auto size = ClientExtent(window);
		suspended = size.Width == 0 || size.Height == 0;

		if (!suspended)
		{
			Build(size);
		}
	}

	void D3D12Swapchain::Build(Rhi::Extent2D size)
	{
		// Re-selected at every build, as on Vulkan, so a display switching HDR mode is followed.
		const auto selected = SelectFormat(Support(state->Factory.Get(), window), request);

		if (!selected)
		{
			suspended = true; // A required-HDR display that left HDR mode stays dormant.
			return;
		}

		const auto buffer = BufferFormat(selected->PixelFormat);

		if (!swapchain)
		{
			DXGI_SWAP_CHAIN_DESC1 chain{};
			chain.Width = size.Width;
			chain.Height = size.Height;
			chain.Format = buffer;
			chain.SampleDesc.Count = 1;
			chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			chain.BufferCount = bufferCount;
			chain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			chain.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
			chain.Flags =
				DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (state->Features.Tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
			ComPtr<IDXGISwapChain1> created;
			Check(state->Factory->CreateSwapChainForHwnd(queue.GetQueue(), window, &chain, nullptr, nullptr, &created),
				"CreateSwapChainForHwnd", state.get());
			Check(created.As(&swapchain), "IDXGISwapChain3", state.get());
			state->Factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
			swapchain->SetMaximumFrameLatency(bufferCount - 1);
			latency = swapchain->GetFrameLatencyWaitableObject();
		}
		else
		{
			DXGI_SWAP_CHAIN_DESC1 chain{};
			swapchain->GetDesc1(&chain);
			Check(swapchain->ResizeBuffers(bufferCount, size.Width, size.Height, buffer, chain.Flags), "IDXGISwapChain::ResizeBuffers",
				state.get());
		}

		Check(swapchain->SetColorSpace1(ToDxgiColorSpace(selected->ColorSpace)), "SetColorSpace1", state.get());
		format = selected->PixelFormat;
		colorSpace = selected->ColorSpace;
		extent = size;
		suspended = false;
		mode = ResolveMode(wantedMode);
		CreateImages();
	}

	D3D12Swapchain::~D3D12Swapchain()
	{
		try
		{
			queue.WaitIdle();
		}
		catch (...)
		{
		}

		ReleaseImages();

		if (latency)
		{
			CloseHandle(latency);
		}
	}

	Rhi::PresentMode D3D12Swapchain::ResolveMode(Rhi::PresentMode wanted) const
	{
		if (wanted == Rhi::PresentMode::Auto)
		{
			return vsync ? Rhi::PresentMode::Fifo : Rhi::PresentMode::Mailbox;
		}

		return wanted == Rhi::PresentMode::Immediate && !state->Features.Tearing ? Rhi::PresentMode::Mailbox : wanted;
	}

	void D3D12Swapchain::CreateImages()
	{
		for (std::uint32_t index = 0; index < bufferCount; ++index)
		{
			ComPtr<ID3D12Resource> buffer;
			Check(swapchain->GetBuffer(index, IID_PPV_ARGS(&buffer)), "IDXGISwapChain::GetBuffer", state.get());
			SetDebugName(buffer.Get(), "Swim swapchain image");
			Rhi::TextureDesc desc{};
			desc.Extent = { extent.Width, extent.Height, 1 };
			desc.PixelFormat = format;
			desc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferDestination;
			images.push_back(std::make_unique<D3D12Texture>(state, std::move(buffer), desc));
			views.push_back(std::make_unique<D3D12TextureView>(state, *images.back(), Rhi::TextureViewDesc{}));
		}
	}

	void D3D12Swapchain::ReleaseImages()
	{
		views.clear();
		images.clear();
	}

	Rhi::SwapchainAcquireResult D3D12Swapchain::AcquireNextImage(Rhi::Semaphore& signalSemaphore, Rhi::Fence* signalFence)
	{
		Rhi::SwapchainAcquireResult result;
		const auto current = ClientExtent(window);

		if (suspended || images.empty() || current.Width == 0 || current.Height == 0)
		{
			result.Suspended = true;
			return result;
		}

		if (current.Width != extent.Width || current.Height != extent.Height)
		{
			result.OutOfDate = true;
			return result;
		}

		if (latency && WaitForSingleObjectEx(latency, 1000, TRUE) != WAIT_OBJECT_0)
		{
			result.NotReady = true;
			return result;
		}

		result.ImageIndex = swapchain->GetCurrentBackBufferIndex();
		// The direct queue orders buffer reuse behind earlier presents; nothing to wait for.
		static_cast<D3D12Semaphore&>(signalSemaphore).SignalReady();

		if (signalFence)
		{
			static_cast<D3D12Fence&>(*signalFence).SignalOnCpu();
		}

		return result;
	}

	bool D3D12Swapchain::Present(Rhi::Queue&, std::uint32_t imageIndex, std::span<Rhi::Semaphore* const> waits)
	{
		if (imageIndex != swapchain->GetCurrentBackBufferIndex())
		{
			throw std::logic_error("D3D12: Present needs the image the last acquire returned");
		}

		std::scoped_lock lock(queue.GetMutex());
		queue.WaitSemaphores(waits);
		const UINT interval = mode == Rhi::PresentMode::Fifo ? 1u : 0u;
		const UINT flags = mode == Rhi::PresentMode::Immediate ? DXGI_PRESENT_ALLOW_TEARING : 0u;
		const HRESULT result = swapchain->Present(interval, flags);

		if (result == DXGI_STATUS_OCCLUDED)
		{
			return true;
		}

		Check(result, "IDXGISwapChain::Present", state.get());
		const auto current = ClientExtent(window);
		return current.Width == extent.Width && current.Height == extent.Height;
	}

	bool D3D12Swapchain::Resize(Rhi::Extent2D newExtent, const Rhi::TimelinePoint& safeAfter)
	{
		if (newExtent.Width == 0 || newExtent.Height == 0)
		{
			suspended = true;
			return false;
		}

		if (safeAfter.Semaphore)
		{
			safeAfter.Semaphore->Wait(safeAfter.Value);
		}

		queue.WaitIdle();
		ReleaseImages();
		Build(newExtent);
		return !suspended;
	}

} // namespace Swim::RhiD3D12
