#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"
#include "Engine/Systems/Renderer/RHI/RhiDiagnostics.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
		// Which GPU renders: "auto" (the adapter driving the window's display when it is a
		// discrete GPU, else the first discrete GPU, else the first suitable one), "display"
		// (always the display's adapter), "discrete", "integrated", or an adapter index or a
		// part of its name.
		std::string Adapter = "auto";
		// Present on a dedicated thread: the present call (which can block for milliseconds,
		// for example when the image crosses to another adapter's display) overlaps the
		// next frame's update and build instead of stalling the main thread.
		bool AsyncPresent = true;
		// With AsyncPresent: present each frame after the next one is submitted. A present
		// call that waits for its frame's GPU work and then copies the image (to another
		// adapter's display) then overlaps the next frame's GPU work instead of idling the
		// GPU; costs a frame of latency.
		bool DeferPresent = false;
		// With AsyncPresent: build and submit the frame before acquiring its image, and draw it
		// into the image with a second small submission (a held copy: one extra full-screen
		// pass). Pays off when presents block until their frame's GPU work is done (an image
		// crossing to another adapter's display); auto does it only then.
		enum class Split : std::uint8_t
		{
			Auto,
			On,
			Off,
		};

		Split SplitPresent = Split::Auto;
	};

	// Presentation timings of the last frame.
	struct PresentStats
	{
		double CallMilliseconds = 0.0; // The present call itself (on the present thread when async).
		double WaitMilliseconds = 0.0; // The main thread waited for the previous present to finish.
		bool Async = false;
		bool Deferred = false;
		bool Split = false; // Frames are built before their image is acquired (RenderDeviceDesc::SplitPresent).
		bool CrossAdapter = false; // The window's display is driven by another adapter than the renderer's.
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

		// The current frame slot's executor for the small submission that draws a frame into
		// its swapchain image after a late acquire (async present).
		Swim::Render::RenderGraphExecutor& GetPresentExecutor() const { return *presentExecutors[slot]; }

		std::uint32_t GetFrameSlot() const { return slot; }

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
		// late: acquired after the frame was built at the current extent (async present), so
		// a pending rebuild skips the frame instead of resizing under it.
		Frame Acquire(bool late = false);

		// Whether the frame can be built before its image is acquired (Acquire(true) at the end).
		bool CanAcquireLate() const { return swapchain && asyncPresent && splitPresent && !resizeRequested && !needsRebuild; }

		const PresentStats& GetPresentStats() const { return presentStats; }

		// The synchronization of the frame's submission (valid until the next Acquire).
		Swim::Rhi::SubmitDesc GetSubmit(const Frame& frame);

		// Presents the acquired image; false requests a rebuild.
		bool Present(const Frame& frame);

		// Records the frame's completion (the safe point of a swapchain rebuild).
		void SetLastCompletion(Swim::Rhi::TimelinePoint completion);

		Swim::Rhi::TimelinePoint GetLastCompletion() const { return lastCompletion; }

		// Submitted frames the GPU has not finished.
		std::uint32_t GetQueuedFrames();

		// Blocks until at most `keep` submitted frames are unfinished (frame pacing; 0: the GPU
		// has finished everything); false on timeout.
		bool WaitForQueuedFrames(std::uint64_t timeoutNanoseconds, std::uint32_t keep = 0);

		// The present mode from the next frame on (the swapchain is rebuilt); headless ignores it.
		void SetPresentMode(Swim::Rhi::PresentMode mode);

		// What the swapchain presents with (Fifo headless).
		Swim::Rhi::PresentMode GetPresentMode() const;

		void WaitIdle();

	  private:

		bool Rebuild();

		void CreatePresentSemaphores();

		// Blocks until the present thread has finished the last present (and takes its result).
		void WaitForPresent();

		void QueuePresent(std::uint32_t image);

		// Presents the deferred image now and waits (before a rebuild or idling).
		void FlushDeferredPresent();

		void PresentLoop();

		std::unique_ptr<Swim::Rhi::GraphicsSystem> graphics;
		std::unique_ptr<Swim::Rhi::Device> device;
		const Swim::Rhi::AdapterInfo* adapterInfo = nullptr;
		std::vector<std::unique_ptr<Swim::Render::RenderGraphExecutor>> executors;
		std::vector<std::unique_ptr<Swim::Render::RenderGraphExecutor>> presentExecutors;
		std::uint32_t slot = 0;
		Swim::Platform::Window* window = nullptr;
		std::unique_ptr<Swim::Rhi::Swapchain> swapchain;
		std::vector<std::unique_ptr<Swim::Rhi::Semaphore>> acquired; // Per frame slot.
		std::vector<std::unique_ptr<Swim::Rhi::Semaphore>> ready;
		std::vector<bool> presented;
		std::array<Swim::Rhi::Semaphore*, 1> waits{};
		std::array<Swim::Rhi::Semaphore*, 1> signals{};
		Swim::Rhi::TimelinePoint lastCompletion{};
		std::deque<Swim::Rhi::TimelinePoint> queued; // Submitted, maybe unfinished.
		// Already-signalled (value 0) retirement point for swapchain rebuilds before the first
		// submission: Resize needs a same-device timeline even when nothing is in flight
		// (Windows sends a resize before the first frame on DPI-scaled displays).
		std::unique_ptr<Swim::Rhi::Timeline> idleTimeline;
		Swim::Rhi::Extent2D headlessExtent{};
		Swim::Rhi::Extent2D requestedExtent{};
		bool resizeRequested = false;
		bool needsRebuild = false;
		bool vsync = true;
		bool asyncPresent = false;
		PresentStats presentStats;
		// The present thread: one present in flight at a time.
		std::thread presentThread;
		std::mutex presentMutex;
		std::condition_variable presentSignal;
		std::uint32_t presentImage = UINT32_MAX;
		bool presentQueued = false;
		bool presentOk = true;
		bool presentStop = false;
		bool deferPresent = false;
		bool splitPresent = false;
		std::uint32_t deferredImage = UINT32_MAX;
		double presentCallMilliseconds = 0.0;

	};

} // namespace Engine
