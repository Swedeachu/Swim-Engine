#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Objects.h"

#include <vector>

namespace Swim::RhiD3D12
{

	Rhi::SwapchainSupport QueryD3D12SwapchainSupport(const D3D12DeviceState& state, Platform::Window& window);
	bool D3D12SwapchainSupportsColorMode(const D3D12DeviceState& state, Platform::Window& window, const Rhi::SwapchainDesc& desc);

	// A DXGI flip-model swapchain presenting on the direct queue. Acquire waits on the frame
	// latency object (DXGI orders buffer reuse itself); Immediate presents with tearing when the
	// display path allows it. Created for a minimized window it stays dormant (no images) until a
	// non-empty Resize.
	class D3D12Swapchain final : public Rhi::Swapchain
	{

	  public:
		D3D12Swapchain(
			std::shared_ptr<D3D12DeviceState> state, D3D12Queue& queue, Platform::Window& window, const Rhi::SwapchainDesc& desc);
		~D3D12Swapchain() override;

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(swapchain.Get()); }

		Rhi::Format GetFormat() const override { return format; }

		Rhi::SwapchainColorSpace GetColorSpace() const override { return colorSpace; }

		Rhi::Extent2D GetExtent() const override { return extent; }

		std::uint32_t GetImageCount() const override { return static_cast<std::uint32_t>(images.size()); }

		Rhi::TextureView& GetImageView(std::uint32_t imageIndex) override { return *views.at(imageIndex); }

		Rhi::SwapchainAcquireResult AcquireNextImage(Rhi::Semaphore& signalSemaphore, Rhi::Fence* signalFence) override;
		bool Present(Rhi::Queue& queue, std::uint32_t imageIndex, std::span<Rhi::Semaphore* const> waits) override;
		bool Resize(Rhi::Extent2D extent, const Rhi::TimelinePoint& safeAfter) override;

		void SetPresentMode(Rhi::PresentMode wanted) override { wantedMode = wanted; }

		Rhi::PresentMode GetPresentMode() const override { return mode; }

	  private:
		void Build(Rhi::Extent2D size);
		void CreateImages();
		void ReleaseImages();
		Rhi::PresentMode ResolveMode(Rhi::PresentMode wanted) const;

		std::shared_ptr<D3D12DeviceState> state;
		D3D12Queue& queue;
		HWND window = nullptr;
		ComPtr<IDXGISwapChain3> swapchain;
		HANDLE latency = nullptr;
		Rhi::Format format = Rhi::Format::BGRA8Unorm;
		Rhi::SwapchainColorSpace colorSpace = Rhi::SwapchainColorSpace::Undefined; // Until the first build.
		Rhi::Extent2D extent{};
		std::uint32_t bufferCount = 3;
		bool vsync = true;
		bool suspended = false;
		Rhi::PresentMode wantedMode = Rhi::PresentMode::Auto;
		Rhi::SwapchainDesc request;
		Rhi::PresentMode mode = Rhi::PresentMode::Fifo;
		std::vector<std::unique_ptr<D3D12Texture>> images;
		std::vector<std::unique_ptr<D3D12TextureView>> views;
	};

} // namespace Swim::RhiD3D12
