#include "Engine/Runtime/FramePacing.h"

#include "Engine/Platform/Thread.h"
#include "Engine/Systems/Renderer/Runtime/RenderDevice.h"

#include <algorithm>
#include <iostream>

namespace Engine
{

	using Swim::Commands::VariableFlags;
	using Clock = std::chrono::steady_clock;

	namespace
	{

		constexpr const char* Category = "Video/Display";

		double Milliseconds(Clock::duration duration)
		{
			return std::chrono::duration<double, std::milli>(duration).count();
		}

		const char* Name(Swim::Rhi::PresentMode mode)
		{
			switch (mode)
			{
			case Swim::Rhi::PresentMode::Mailbox:
				return "mailbox";
			case Swim::Rhi::PresentMode::Immediate:
				return "immediate";
			case Swim::Rhi::PresentMode::Fifo:
				return "vsync";
			default:
				return "auto";
			}
		}

	} // namespace

	FramePacing::FramePacing(Swim::Commands::CommandRegistry& commandsValue) : commands(commandsValue)
	{
		presentMode = &commands.AddEnum("video_present_mode", { "auto", "vsync", "mailbox", "immediate" }, 0,
			"How frames are presented: vsync, mailbox (no tearing, newest frame), immediate (lowest latency, may tear); auto follows --vsync",
			Category, VariableFlags::Saved);
		fpsMax = &commands.AddInt("video_fps_max", 0, 0, 1000, "Frame rate cap (0 = uncapped)", Category, VariableFlags::Saved);
		lowLatency = &commands.AddBool("video_low_latency", false,
			"Wait for the GPU before sampling input, keeping at most one frame queued (lower latency, lower throughput)", Category,
			VariableFlags::Saved);
		presentMode->Subscribe(
			[this](const Swim::Commands::ConsoleVariable&)
			{
				ApplyPresentMode();
			});
		commands.Register({ "video_pacing_stats", "Prints frame time, waits, latency, queued frames and the present mode", Category, "" },
			[this](const std::vector<std::string>&)
			{
				std::cout << "[Pacing] frame " << stats.FrameMilliseconds << " ms, cap wait " << stats.CapWaitMilliseconds << " ms, GPU wait "
						  << stats.GpuWaitMilliseconds << " ms, latency " << stats.LatencyMilliseconds << " ms, queued " << stats.QueuedFrames
						  << ", present " << Name(stats.PresentMode) << '\n';
			});
	}

	FramePacing::~FramePacing()
	{
		commands.Unregister("video_pacing_stats");
	}

	void FramePacing::SetDevice(RenderDevice* deviceValue)
	{
		device = deviceValue;
		applied = Swim::Rhi::PresentMode::Auto;
		pending.clear();
		lastSubmitted = 0;
		ApplyPresentMode();
	}

	void FramePacing::ApplyPresentMode()
	{
		if (!device)
		{
			return;
		}

		using Swim::Rhi::PresentMode;
		constexpr PresentMode modes[] = { PresentMode::Auto, PresentMode::Fifo, PresentMode::Mailbox, PresentMode::Immediate };
		const auto wanted = modes[std::min<std::uint32_t>(presentMode->GetEnum(), 3)];

		// Auto at startup keeps the mode the device was created with (--vsync).
		if (wanted != applied)
		{
			device->SetPresentMode(wanted);
			applied = wanted;
		}
	}

	void FramePacing::BeginFrame()
	{
		stats.CapWaitMilliseconds = 0.0;
		stats.GpuWaitMilliseconds = 0.0;
		const auto cap = fpsMax->GetInt();

		if (cap > 0 && haveLast)
		{
			const auto deadline = lastStart + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / double(cap)));
			const auto before = Clock::now();

			if (before < deadline)
			{
				Swim::Platform::SleepPrecise(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - before).count()));
				stats.CapWaitMilliseconds = Milliseconds(Clock::now() - before);
			}
		}

		if (lowLatency->GetBool() && device)
		{
			const auto before = Clock::now();
			device->WaitForQueuedFrames(100'000'000);
			stats.GpuWaitMilliseconds = Milliseconds(Clock::now() - before);
		}

		const auto start = Clock::now();

		if (haveLast)
		{
			stats.FrameMilliseconds = Milliseconds(start - lastStart);
		}

		lastStart = start;
		haveLast = true;
		inputTime = start;

		if (device)
		{
			stats.QueuedFrames = device->GetQueuedFrames();
			stats.PresentMode = device->GetPresentMode();

			while (!pending.empty() && pending.front().Completion.Semaphore->GetCompletedValue() >= pending.front().Completion.Value)
			{
				const double sample = Milliseconds(start - pending.front().Input);
				stats.LatencyMilliseconds = stats.LatencyMilliseconds == 0.0 ? sample : stats.LatencyMilliseconds * 0.9 + sample * 0.1;
				pending.pop_front();
			}
		}
	}

	void FramePacing::EndFrame()
	{
		if (!device)
		{
			return;
		}

		const auto completion = device->GetLastCompletion();

		if (completion.Semaphore && completion.Value != lastSubmitted)
		{
			lastSubmitted = completion.Value;
			pending.push_back({ completion, inputTime });

			if (pending.size() > 8)
			{
				pending.pop_front();
			}
		}
	}

} // namespace Engine
