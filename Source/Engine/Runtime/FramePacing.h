#pragma once

#include "Engine/Commands/CommandRegistry.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <chrono>
#include <cstdint>
#include <deque>

namespace Engine
{

	class RenderDevice;

	struct FramePacingStats
	{
		double FrameMilliseconds = 0.0;	  // Start to start of the last frame.
		double CapWaitMilliseconds = 0.0; // Slept by the frame cap this frame.
		double GpuWaitMilliseconds = 0.0; // Waited for the GPU this frame (video_low_latency).
		// Input sampled -> the GPU finished that frame, smoothed. Without video_low_latency it
		// is observed once per frame, so it can read up to one frame long.
		double LatencyMilliseconds = 0.0;
		std::uint32_t QueuedFrames = 0; // Submitted, unfinished frames when input was sampled.
		Swim::Rhi::PresentMode PresentMode = Swim::Rhi::PresentMode::Fifo;
		double PresentCallMilliseconds = 0.0; // The present call (on the present thread when async).
		double PresentWaitMilliseconds = 0.0; // The main thread waited for a present this frame.
		bool AsyncPresent = false;
		bool CrossAdapter = false; // Presents cross to another adapter's display (slower presents).
		bool SplitPresent = false; // Frames are submitted before their image is acquired.
	};

	// Present mode, frame cap and low-latency pacing as saved console variables:
	//   video_present_mode auto|vsync|mailbox|immediate  auto follows --vsync
	//   video_fps_max <n>       0 = uncapped; else sleep (then spin) to n frames per second
	//   video_low_latency 0|1   before input is sampled, wait for the GPU to finish the frames
	//                           already submitted, so the render queue is at most one frame deep
	//   video_pacing_stats      prints the pacing statistics
	// BeginFrame runs before input is pumped (the waits come first, then input is sampled);
	// EndFrame after the frame is submitted.
	class FramePacing
	{

	  public:

		explicit FramePacing(Swim::Commands::CommandRegistry& commands);

		~FramePacing();

		FramePacing(const FramePacing&) = delete;

		FramePacing& operator=(const FramePacing&) = delete;

		// The device the present mode applies to (null: none, headless frames still pace).
		void SetDevice(RenderDevice* device);

		void BeginFrame();

		void EndFrame();

		const FramePacingStats& GetStats() const { return stats; }

	  private:

		void ApplyPresentMode();

		struct Pending
		{
			Swim::Rhi::TimelinePoint Completion;
			std::chrono::steady_clock::time_point Input;
		};

		Swim::Commands::CommandRegistry& commands;
		Swim::Commands::ConsoleVariable* presentMode = nullptr;
		Swim::Commands::ConsoleVariable* fpsMax = nullptr;
		Swim::Commands::ConsoleVariable* lowLatency = nullptr;
		RenderDevice* device = nullptr;
		Swim::Rhi::PresentMode applied = Swim::Rhi::PresentMode::Auto;
		FramePacingStats stats;
		std::chrono::steady_clock::time_point lastStart{};
		std::chrono::steady_clock::time_point inputTime{};
		bool haveLast = false;
		std::uint64_t lastSubmitted = 0;
		std::deque<Pending> pending;

	};

} // namespace Engine
