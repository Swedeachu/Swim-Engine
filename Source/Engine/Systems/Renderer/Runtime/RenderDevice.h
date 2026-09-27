#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Engine/Systems/Renderer/RHI/RhiDiagnostics.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Swim::Platform
{
	class Window;
}

namespace Engine
{
	struct RenderDeviceDesc
	{
		// The window to present to; null renders headless (offscreen, no swapchain).
		Swim::Platform::Window* Window = nullptr;
		std::uint32_t Width = 1280; // Headless size, and the initial size of a window.
		std::uint32_t Height = 720;
		bool VSync = false;
		bool Validation = false;
		// Frames the CPU may record ahead of the GPU (1 or 2). With 2, frame N+1 is built
		// and recorded while the GPU still renders frame N: each frame slot has its own
		// render-graph executor (commands, transient pool, staging arenas, timestamps) and
		// swapchain acquire semaphore, and a slot waits only for its own previous frame.
		std::uint32_t FramesInFlight = 2;
	};

	// The GPU the engine renders with (Phase 23): the RHI graphics system and device
	// (Vulkan), the render-graph executor every subsystem records into, and, with a
	// window, the swapchain with its per-image present semaphores. Acquire / Present /
	// Resize follow the RHI swapchain contract: an out-of-date or suspended swapchain
	// skips the frame and is rebuilt at the next Acquire.
	class RenderDevice
	{
	  public:
		// Throws std::runtime_error when no suitable device (bindless descriptors) exists.
		explicit RenderDevice(const RenderDeviceDesc& desc);
		~RenderDevice();
		RenderDevice(const RenderDevice&) = delete;
		RenderDevice& operator=(const RenderDevice&) = delete;

		Swim::Rhi::Device& GetDevice() const { return *device; }

		// The current frame slot's executor.
		Swim::Render::RenderGraphExecutor& GetExecutor() const { return *executors[slot]; }

		// Moves to the next frame slot (FrameRenderer::Render, once per frame, before it
		// waits for that slot's previous frame).
		void AdvanceFrameSlot() { slot = (slot + 1) % static_cast<std::uint32_t>(executors.size()); }

		std::uint32_t GetFramesInFlight() const { return static_cast<std::uint32_t>(executors.size()); }

		const Swim::Rhi::AdapterInfo& GetAdapterInfo() const { return *adapterInfo; }

		const Swim::Rhi::GraphicsCapabilities& GetCapabilities() const { return adapterInfo->Capabilities; }

		bool IsValidationEnabled() const;
		std::uint64_t GetValidationErrorCount() const;

		bool IsHeadless() const { return swapchain == nullptr; }

		Swim::Rhi::Format GetSwapchainFormat() const;
		// The size frames are rendered at (swapchain extent or headless size).
		Swim::Rhi::Extent2D GetExtent() const;

		// Requests a new window size (pixels); applied at the next Acquire. Zero suspends.
		void RequestResize(std::uint32_t width, std::uint32_t height);

		struct Frame
		{
			bool Valid = false;
			std::uint32_t Image = UINT32_MAX;
			Swim::Rhi::Texture* Target = nullptr; // The swapchain image (null headless).
			bool Presented = false;				  // The image was presented before (its state is Present).
			bool Suboptimal = false;
		};

		// Headless always succeeds with no target. Invalid while minimized or rebuilding.
		Frame Acquire();
		// The synchronization of the frame's submission (valid until the next Acquire).
		Swim::Rhi::SubmitDesc GetSubmit(const Frame& frame);
		// Presents the acquired image; false requests a rebuild.
		bool Present(const Frame& frame);

		// Records the frame's completion (the safe point of a swapchain rebuild).
		void SetLastCompletion(Swim::Rhi::TimelinePoint completion) { lastCompletion = completion; }

		void WaitIdle();

	  private:
		bool Rebuild();
		void CreatePresentSemaphores();

		std::unique_ptr<Swim::Rhi::GraphicsSystem> graphics;
		std::unique_ptr<Swim::Rhi::Device> device;
		const Swim::Rhi::AdapterInfo* adapterInfo = nullptr;
		std::vector<std::unique_ptr<Swim::Render::RenderGraphExecutor>> executors;
		std::uint32_t slot = 0;
		Swim::Platform::Window* window = nullptr;
		std::unique_ptr<Swim::Rhi::Swapchain> swapchain;
		std::vector<std::unique_ptr<Swim::Rhi::Semaphore>> acquired; // Per frame slot.
		std::vector<std::unique_ptr<Swim::Rhi::Semaphore>> ready;
		std::vector<bool> presented;
		std::array<Swim::Rhi::Semaphore*, 1> waits{};
		std::array<Swim::Rhi::Semaphore*, 1> signals{};
		Swim::Rhi::TimelinePoint lastCompletion{};
		// Already-signalled (value 0) retirement point for swapchain rebuilds before the first
		// submission: Resize needs a same-device timeline even when nothing is in flight
		// (Windows sends a resize before the first frame on DPI-scaled displays).
		std::unique_ptr<Swim::Rhi::Timeline> idleTimeline;
		Swim::Rhi::Extent2D headlessExtent{};
		Swim::Rhi::Extent2D requestedExtent{};
		bool resizeRequested = false;
		bool needsRebuild = false;
		bool vsync = true;
	};
} // namespace Engine
